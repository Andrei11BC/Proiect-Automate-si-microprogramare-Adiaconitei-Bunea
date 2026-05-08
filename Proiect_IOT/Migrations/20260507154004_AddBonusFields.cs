using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Proiect_IOT.Migrations
{
    /// <inheritdoc />
    public partial class AddBonusFields : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<long>(
                name: "AgeMs",
                table: "SensorData",
                type: "INTEGER",
                nullable: false,
                defaultValue: 0L);

            migrationBuilder.AddColumn<double>(
                name: "Latitude",
                table: "SensorData",
                type: "REAL",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "Longitude",
                table: "SensorData",
                type: "REAL",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "AgeMs",
                table: "SensorData");

            migrationBuilder.DropColumn(
                name: "Latitude",
                table: "SensorData");

            migrationBuilder.DropColumn(
                name: "Longitude",
                table: "SensorData");
        }
    }
}
