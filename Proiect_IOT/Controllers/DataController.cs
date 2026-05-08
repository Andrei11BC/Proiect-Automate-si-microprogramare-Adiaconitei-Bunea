using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using Proiect_IOT.Data;
using Proiect_IOT.Models;

namespace Proiect_IOT.Controllers
{
    [Route("api/[controller]")]
    [ApiController]
    public class DataController : ControllerBase
    {
        private readonly AppDbContext _context;
        private readonly IConfiguration _configuration;

        public DataController(AppDbContext context, IConfiguration configuration)
        {
            _context = context;
            _configuration = configuration;
        }

        private static DateTime RomaniaNow()
        {
            return TimeZoneInfo.ConvertTimeFromUtc(
                DateTime.UtcNow,
                TimeZoneInfo.FindSystemTimeZoneById("E. Europe Standard Time")
            );
        }

        private string ExpectedDeviceKey => _configuration["DeviceSecurity:DeviceKey"] ?? "FRDM-SECRET-2026";

        private bool IsAuthorizedDevice(string? deviceKey)
        {
            return !string.IsNullOrWhiteSpace(deviceKey) && deviceKey == ExpectedDeviceKey;
        }

        [HttpGet("ping")]
        public IActionResult Ping()
        {
            return Ok(new
            {
                message = "API functional",
                timeLocal = RomaniaNow()
            });
        }

        [HttpGet]
        public async Task<IActionResult> GetData([FromQuery] int take = 500)
        {
            take = Math.Clamp(take, 1, 5000);

            var data = await _context.SensorData
                .OrderByDescending(x => x.Timestamp)
                .Take(take)
                .Select(x => new
                {
                    x.Id,
                    x.DeviceId,
                    x.Temperature,
                    x.GasLevel,
                    x.Latitude,
                    x.Longitude,
                    x.AgeMs,
                    x.Timestamp
                })
                .ToListAsync();

            return Ok(data);
        }


        [HttpGet("range")]
        public async Task<IActionResult> GetRange(
            [FromQuery] string? date,
            [FromQuery] string? startTime,
            [FromQuery] string? endTime,
            [FromQuery] int take = 5000)
        {
            take = Math.Clamp(take, 1, 20000);

            DateTime selectedDate;

            if (string.IsNullOrWhiteSpace(date))
            {
                selectedDate = RomaniaNow().Date;
            }
            else if (!DateTime.TryParse(date, out selectedDate))
            {
                return BadRequest(new { message = "Data selectata nu este valida. Foloseste formatul yyyy-MM-dd." });
            }
            else
            {
                selectedDate = selectedDate.Date;
            }

            TimeSpan start = TimeSpan.Zero;
            TimeSpan end = new TimeSpan(23, 59, 59);

            if (!string.IsNullOrWhiteSpace(startTime) && !TimeSpan.TryParse(startTime, out start))
            {
                return BadRequest(new { message = "Ora de inceput nu este valida. Foloseste formatul HH:mm." });
            }

            if (!string.IsNullOrWhiteSpace(endTime) && !TimeSpan.TryParse(endTime, out end))
            {
                return BadRequest(new { message = "Ora de final nu este valida. Foloseste formatul HH:mm." });
            }

            if (end <= start)
            {
                return BadRequest(new { message = "Ora de final trebuie sa fie mai mare decat ora de inceput." });
            }

            var from = selectedDate.Add(start);
            var to = selectedDate.Add(end);

            var query = _context.SensorData
                .Where(x => x.Timestamp >= from && x.Timestamp <= to)
                .OrderBy(x => x.Timestamp)
                .Take(take);

            var items = await query
                .Select(x => new
                {
                    x.Id,
                    x.DeviceId,
                    x.Temperature,
                    x.GasLevel,
                    x.Latitude,
                    x.Longitude,
                    x.AgeMs,
                    x.Timestamp
                })
                .ToListAsync();

            var temperatures = items.Select(x => (double)x.Temperature).ToList();

            return Ok(new
            {
                selectedDate = selectedDate.ToString("yyyy-MM-dd"),
                from,
                to,
                count = items.Count,
                average = temperatures.Count > 0 ? Math.Round(temperatures.Average(), 2) : (double?)null,
                minimum = temperatures.Count > 0 ? Math.Round(temperatures.Min(), 2) : (double?)null,
                maximum = temperatures.Count > 0 ? Math.Round(temperatures.Max(), 2) : (double?)null,
                items
            });
        }

        [HttpGet("latest")]
        public async Task<IActionResult> GetLatest()
        {
            var latest = await _context.SensorData
                .OrderByDescending(x => x.Timestamp)
                .Select(x => new
                {
                    x.Id,
                    x.DeviceId,
                    x.Temperature,
                    x.GasLevel,
                    x.Latitude,
                    x.Longitude,
                    x.AgeMs,
                    x.Timestamp
                })
                .FirstOrDefaultAsync();

            if (latest == null)
            {
                return NotFound(new { message = "Nu exista date in baza de date." });
            }

            return Ok(latest);
        }

        [HttpGet("statistics")]
        public async Task<IActionResult> GetStatistics()
        {
            var now = RomaniaNow();

            var periods = new[]
            {
                new PeriodDefinition("10m", "Ultimele 10 minute", TimeSpan.FromMinutes(10)),
                new PeriodDefinition("30m", "Ultimele 30 minute", TimeSpan.FromMinutes(30)),
                new PeriodDefinition("1h", "Ultima ora", TimeSpan.FromHours(1)),
                new PeriodDefinition("1d", "Ultima zi", TimeSpan.FromDays(1)),
                new PeriodDefinition("7d", "Ultima saptamana", TimeSpan.FromDays(7)),
                new PeriodDefinition("30d", "Ultima luna", TimeSpan.FromDays(30))
            };

            var result = new List<object>();

            foreach (var period in periods)
            {
                var from = now.Subtract(period.Duration);

                var values = await _context.SensorData
                    .Where(x => x.Timestamp >= from)
                    .Select(x => x.Temperature)
                    .ToListAsync();

                result.Add(new
                {
                    period.Key,
                    period.Label,
                    Count = values.Count,
                    Average = values.Count > 0 ? Math.Round(values.Average(), 2) : (double?)null,
                    Minimum = values.Count > 0 ? Math.Round(values.Min(), 2) : (double?)null,
                    Maximum = values.Count > 0 ? Math.Round(values.Max(), 2) : (double?)null
                });
            }

            return Ok(result);
        }

        [HttpGet("forecast")]
        public async Task<IActionResult> GetForecast([FromQuery] int take = 4000)
        {
            take = Math.Clamp(take, 10, 10000);

            var rows = await _context.SensorData
                .OrderByDescending(x => x.Timestamp)
                .Take(take)
                .OrderBy(x => x.Timestamp)
                .Select(x => new
                {
                    x.Timestamp,
                    x.Temperature,
                    x.Latitude,
                    x.Longitude
                })
                .ToListAsync();

            if (rows.Count < 10)
            {
                return Ok(new
                {
                    available = false,
                    message = "Sunt necesare cel putin 10 masuratori pentru o prognoza relevanta."
                });
            }

            var firstTimestamp = rows.First().Timestamp;
            var lastTimestamp = rows.Last().Timestamp;
            var lastTemperature = rows.Last().Temperature;

            double latitude = rows.LastOrDefault(x => x.Latitude.HasValue)?.Latitude ?? 45.793;
            double longitude = rows.LastOrDefault(x => x.Longitude.HasValue)?.Longitude ?? 24.152;
            var locality = GuessLocality(latitude, longitude);

            var points = rows
                .Select(x => new RegressionPoint
                {
                    X = Math.Max(0.0, (x.Timestamp - firstTimestamp).TotalHours),
                    Y = x.Temperature
                })
                .ToList();

            var regression = LinearRegressionByTime(points);

            double observedHours = Math.Max(1.0, (lastTimestamp - firstTimestamp).TotalHours);
            double xNow = Math.Max(0.0, (lastTimestamp - firstTimestamp).TotalHours);

            double trendPerHour = regression.Slope;
            double trendPerDay = trendPerHour * 24.0;

            string confidence = rows.Count switch
            {
                >= 3000 => "Buna",
                >= 1000 => "Medie",
                _ => "Scazuta"
            };

            string warning = observedHours < 24
                ? "Atentie: datele analizate acopera mai putin de 24 de ore. Prognoza pe saptamana/luna este o extrapolare tehnica, nu o prognoza meteo reala."
                : "Prognoza este calculata prin regresie liniara pe istoricul disponibil.";

            return Ok(new
            {
                available = true,
                method = "Regresie liniara pe timp: temperatura = a * ora + b",
                samples = rows.Count,
                generatedAt = RomaniaNow(),
                firstTimestamp,
                lastTimestamp,
                observedHours = Math.Round(observedHours, 2),
                lastTemperature = Math.Round(lastTemperature, 2),
                trendPerHour = Math.Round(trendPerHour, 5),
                trendPerDay = Math.Round(trendPerDay, 3),
                confidence,
                warning,
                location = new
                {
                    latitude = Math.Round(latitude, 6),
                    longitude = Math.Round(longitude, 6),
                    locality
                },
                forecast = new[]
                {
                    BuildForecastItem("1d", "Peste 1 zi", PredictSafe(regression, xNow + 24.0), confidence, trendPerDay),
                    BuildForecastItem("7d", "Peste 1 saptamana", PredictSafe(regression, xNow + 24.0 * 7.0), confidence, trendPerDay),
                    BuildForecastItem("30d", "Peste 1 luna", PredictSafe(regression, xNow + 24.0 * 30.0), confidence, trendPerDay)
                }
            });
        }

        [HttpGet("linear-regression")]
        public IActionResult LinearRegressionFromPredefinedData()
        {
            var predefinedData = new List<double> { 22.1, 22.3, 22.6, 22.9, 23.1, 23.4, 23.6 };
            var prediction = LinearRegressionPredict(predefinedData);

            return Ok(new
            {
                method = "Regresie liniara simpla pe set de date predefinit",
                formula = "y = a * x + b",
                predefinedData,
                slope = Math.Round(prediction.Slope, 4),
                intercept = Math.Round(prediction.Intercept, 4),
                predictedNextValue = Math.Round(prediction.PredictedNext, 2)
            });
        }

        [HttpPost]
        public async Task<IActionResult> PostData([FromBody] SensorDataRequest incomingData)
        {
            if (!ModelState.IsValid)
            {
                return BadRequest(new
                {
                    message = "Request invalid. Verifica tipurile JSON, de exemplu Temperature trebuie sa fie numar, nu text."
                });
            }

            if (incomingData == null)
            {
                return BadRequest(new { message = "Datele nu au fost primite corect." });
            }

            if (!IsAuthorizedDevice(incomingData.DeviceKey))
            {
                return Unauthorized(new { message = "DeviceKey invalid. Doar placutele autorizate pot trimite date." });
            }

            if (incomingData.Temperature == null)
            {
                return BadRequest(new { message = "Campul Temperature lipseste din request." });
            }

            if (incomingData.Temperature < -50 || incomingData.Temperature > 100)
            {
                return BadRequest(new { message = "Temperatura este in afara intervalului acceptat." });
            }

            if (incomingData.Latitude is < -90 or > 90)
            {
                return BadRequest(new { message = "Latitude trebuie sa fie intre -90 si 90." });
            }

            if (incomingData.Longitude is < -180 or > 180)
            {
                return BadRequest(new { message = "Longitude trebuie sa fie intre -180 si 180." });
            }

            var ageMs = Math.Clamp(incomingData.AgeMs, 0, 24L * 60L * 60L * 1000L);

            var timestamp = RomaniaNow().AddMilliseconds(-ageMs);

            var sensorData = new SensorData
            {
                DeviceId = string.IsNullOrWhiteSpace(incomingData.DeviceId)
                    ? "FRDM-MCXN947"
                    : incomingData.DeviceId,
                Temperature = (float)incomingData.Temperature.Value,
                GasLevel = incomingData.GasLevel.HasValue ? (float)incomingData.GasLevel.Value : 0,
                Latitude = incomingData.Latitude,
                Longitude = incomingData.Longitude,
                AgeMs = ageMs,
                Timestamp = timestamp
            };

            _context.SensorData.Add(sensorData);
            await _context.SaveChangesAsync();

            return Ok(new
            {
                message = "Datele au fost salvate cu succes in DB!",
                data = new
                {
                    sensorData.Id,
                    sensorData.DeviceId,
                    sensorData.Temperature,
                    sensorData.GasLevel,
                    sensorData.Latitude,
                    sensorData.Longitude,
                    sensorData.AgeMs,
                    sensorData.Timestamp
                }
            });
        }

        [HttpPost("delete-all")]
        public async Task<IActionResult> DeleteAll([FromBody] DeleteAllRequest request)
        {
            var configuredPassword = _configuration["AdminPassword"] ?? "ulbs2026";

            if (request == null || request.Password != configuredPassword)
            {
                return Unauthorized(new
                {
                    message = "Parola incorecta. Stergerea a fost blocata."
                });
            }

            var rows = await _context.SensorData.ToListAsync();
            var deletedCount = rows.Count;

            if (deletedCount > 0)
            {
                _context.SensorData.RemoveRange(rows);
                await _context.SaveChangesAsync();
            }

            return Ok(new
            {
                message = "Toate intrarile au fost sterse.",
                deletedCount
            });
        }

        [HttpPost("delete-by-temperature")]
        public async Task<IActionResult> DeleteByTemperature([FromBody] DeleteByTemperatureRequest request)
        {
            var configuredPassword = _configuration["AdminPassword"] ?? "ulbs2026";

            if (request == null || request.Password != configuredPassword)
            {
                return Unauthorized(new
                {
                    message = "Parola incorecta. Stergerea a fost blocata."
                });
            }

            if (request.Mode != "greater" && request.Mode != "less")
            {
                return BadRequest(new
                {
                    message = "Mod invalid. Foloseste greater sau less."
                });
            }

            var limit = (float)request.TemperatureLimit;

            IQueryable<SensorData> query = _context.SensorData;

            if (request.Mode == "greater")
            {
                query = query.Where(x => x.Temperature > limit);
            }
            else
            {
                query = query.Where(x => x.Temperature < limit);
            }

            var rows = await query.ToListAsync();
            var deletedCount = rows.Count;

            if (deletedCount > 0)
            {
                _context.SensorData.RemoveRange(rows);
                await _context.SaveChangesAsync();
            }

            return Ok(new
            {
                message = request.Mode == "greater"
                    ? $"Au fost sterse intrarile cu temperatura mai mare decat {limit} °C."
                    : $"Au fost sterse intrarile cu temperatura mai mica decat {limit} °C.",
                deletedCount
            });
        }

        private static LinearRegressionResult LinearRegressionPredict(IReadOnlyList<double> values)
        {
            int n = values.Count;

            if (n == 0)
            {
                return new LinearRegressionResult(0, 0, 0, 0, 0);
            }

            if (n == 1)
            {
                return new LinearRegressionResult(0, values[0], values[0], values[0], values[0]);
            }

            double sumX = 0;
            double sumY = 0;
            double sumXY = 0;
            double sumX2 = 0;

            for (int i = 0; i < n; i++)
            {
                double x = i + 1;
                double y = values[i];

                sumX += x;
                sumY += y;
                sumXY += x * y;
                sumX2 += x * x;
            }

            double denominator = n * sumX2 - sumX * sumX;

            if (Math.Abs(denominator) < 0.000001)
            {
                var last = values[^1];
                return new LinearRegressionResult(0, last, last, last, last);
            }

            double slope = (n * sumXY - sumX * sumY) / denominator;
            double intercept = (sumY - slope * sumX) / n;

            double Predict(double x) => slope * x + intercept;

            return new LinearRegressionResult(
                slope,
                intercept,
                Predict(n + 1),
                Predict(n + 2),
                Predict(n + 3));
        }

        private static object BuildForecastItem(
            string key,
            string label,
            double predictedValue,
            string confidence,
            double trendPerDay)
        {
            double margin = key switch
            {
                "1d" => 0.8,
                "7d" => 1.8,
                "30d" => 3.5,
                _ => 1.0
            };

            return new
            {
                Key = key,
                Label = label,
                PredictedAverage = Math.Round(predictedValue, 2),
                PredictedMinimum = Math.Round(predictedValue - margin, 2),
                PredictedMaximum = Math.Round(predictedValue + margin, 2),
                TrendPerDay = Math.Round(trendPerDay, 3),
                Confidence = confidence
            };
        }

        private static double PredictSafe(LinearRegressionTimeResult regression, double x)
        {
            var value = regression.Slope * x + regression.Intercept;

            // Protectie la extrapolari extreme. Ramane regresie liniara, dar limitata la valori ambientale plauzibile.
            return Math.Clamp(value, -30.0, 60.0);
        }

        private static LinearRegressionTimeResult LinearRegressionByTime(IReadOnlyList<RegressionPoint> points)
        {
            int n = points.Count;

            if (n == 0)
            {
                return new LinearRegressionTimeResult(0, 0);
            }

            if (n == 1)
            {
                return new LinearRegressionTimeResult(0, points[0].Y);
            }

            double sumX = 0;
            double sumY = 0;
            double sumXY = 0;
            double sumX2 = 0;

            foreach (var point in points)
            {
                sumX += point.X;
                sumY += point.Y;
                sumXY += point.X * point.Y;
                sumX2 += point.X * point.X;
            }

            double denominator = n * sumX2 - sumX * sumX;

            if (Math.Abs(denominator) < 0.000001)
            {
                return new LinearRegressionTimeResult(0, points.Last().Y);
            }

            double slope = (n * sumXY - sumX * sumY) / denominator;
            double intercept = (sumY - slope * sumX) / n;

            // Limitare trend ca sa nu rezulte valori aberante pe 30 zile din date colectate doar cateva ore.
            slope = Math.Clamp(slope, -0.15, 0.15);

            return new LinearRegressionTimeResult(slope, intercept);
        }

        private static string GuessLocality(double latitude, double longitude)
        {
            var knownLocations = new[]
            {
                new KnownLocation("Sibiu", 45.793, 24.152),
                new KnownLocation("Bacau", 46.567, 26.914),
                new KnownLocation("Bucuresti", 44.4268, 26.1025),
                new KnownLocation("Cluj-Napoca", 46.7712, 23.6236),
                new KnownLocation("Brasov", 45.6579, 25.6012),
                new KnownLocation("Iasi", 47.1585, 27.6014),
                new KnownLocation("Timisoara", 45.7489, 21.2087)
            };

            var closest = knownLocations
                .Select(x => new
                {
                    x.Name,
                    DistanceKm = DistanceKm(latitude, longitude, x.Latitude, x.Longitude)
                })
                .OrderBy(x => x.DistanceKm)
                .First();

            if (closest.DistanceKm <= 30)
            {
                return closest.Name;
            }

            return $"Localitate necunoscuta - cel mai apropiat reper: {closest.Name}, aprox. {Math.Round(closest.DistanceKm, 1)} km";
        }

        private static double DistanceKm(double lat1, double lon1, double lat2, double lon2)
        {
            const double earthRadiusKm = 6371.0;

            double dLat = DegreesToRadians(lat2 - lat1);
            double dLon = DegreesToRadians(lon2 - lon1);

            double a =
                Math.Sin(dLat / 2) * Math.Sin(dLat / 2) +
                Math.Cos(DegreesToRadians(lat1)) *
                Math.Cos(DegreesToRadians(lat2)) *
                Math.Sin(dLon / 2) *
                Math.Sin(dLon / 2);

            double c = 2 * Math.Atan2(Math.Sqrt(a), Math.Sqrt(1 - a));

            return earthRadiusKm * c;
        }

        private static double DegreesToRadians(double degrees)
        {
            return degrees * Math.PI / 180.0;
        }

        private sealed class RegressionPoint
        {
            public double X { get; set; }
            public double Y { get; set; }
        }

        private sealed record LinearRegressionTimeResult(double Slope, double Intercept);

        private sealed record KnownLocation(
            string Name,
            double Latitude,
            double Longitude);

        public sealed class SensorDataRequest
        {
            public string DeviceKey { get; set; } = string.Empty;
            public string DeviceId { get; set; } = string.Empty;
            public double? Temperature { get; set; }
            public double? GasLevel { get; set; }
            public double? Latitude { get; set; }
            public double? Longitude { get; set; }
            public long AgeMs { get; set; } = 0;
        }

        private sealed record LinearRegressionResult(
            double Slope,
            double Intercept,
            double PredictedNext,
            double PredictedNext2,
            double PredictedNext3);

        private sealed record PeriodDefinition(string Key, string Label, TimeSpan Duration);

        public sealed class DeleteAllRequest
        {
            public string Password { get; set; } = string.Empty;
        }

        public sealed class DeleteByTemperatureRequest
        {
            public string Password { get; set; } = string.Empty;
            public double TemperatureLimit { get; set; }
            public string Mode { get; set; } = string.Empty;
        }
    }
}
