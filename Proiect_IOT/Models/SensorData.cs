namespace Proiect_IOT.Models
{
    public class SensorData
    {
        public int Id { get; set; }
        public string DeviceId { get; set; } = string.Empty;
        public float Temperature { get; set; }
        public float GasLevel { get; set; }
        public double? Latitude { get; set; }
        public double? Longitude { get; set; }
        public long AgeMs { get; set; }
        public DateTime Timestamp { get; set; } = DateTime.UtcNow;
    }
}
