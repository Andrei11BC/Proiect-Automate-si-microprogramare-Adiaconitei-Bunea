using Microsoft.EntityFrameworkCore;
using Proiect_IOT.Models;

namespace Proiect_IOT.Data
{
    public class AppDbContext : DbContext
    {
        public AppDbContext(DbContextOptions<AppDbContext> options) : base(options) { }

        // Aceasta va deveni tabela ta din baza de date
        public DbSet<SensorData> SensorData { get; set; }
    }
}
