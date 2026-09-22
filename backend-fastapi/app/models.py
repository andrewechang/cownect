from datetime import datetime
#allows for the current date and time to be recorded in the database

#sqlalchemy is used to interact with database without SQL queries
from sqlalchemy import DateTime, Float, Integer, String, func
from sqlalchemy.orm import Mapped, mapped_column

#contains Python classes that represent database tables
from .database import Base


#Python class represents the sensor_readings PostgreSQL table.
class SensorReading(Base):
    # Name of the table inside PostgreSQL.
    __tablename__ = "sensor_readings"

    # Automatically generated new row number.
    id: Mapped[int] = mapped_column(primary_key=True)

    # Identifies which collar sent the reading.
    collar_id: Mapped[str] = mapped_column(
        String(50),
        index=True,
    )

    # Time when the collar recorded the sensor data.
    recorded_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
    )

    # GPS coordinates
    latitude: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    longitude: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    #cow thermistor temperature
    cow_temperature_c: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    #circuit board temperature
    board_temperature_c: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    # acceleration derivatives calculated by jetson
    accel_x_derivative: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    accel_y_derivative: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    accel_z_derivative: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    # Processed analog measurement from the microphone.
    microphone_level_derivative: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    # Collar battery voltage.
    battery_voltage: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    # Activity predicted by the Jetson's ML model.
    activity: Mapped[str | None] = mapped_column(
        String(50),
        nullable=True,
    )

    # Confidence of the ML prediction, such as 0.93.
    confidence: Mapped[float | None] = mapped_column(
        Float,
        nullable=True,
    )

    # Time when the API saved the row in PostgreSQL.
    # PostgreSQL fills this in automatically.
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        server_default=func.now(),
    )