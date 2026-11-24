
from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class SensorReadingCreate(BaseModel):
    collar_id: str
    recorded_at: datetime

    latitude: float | None = None
    longitude: float | None = None

    cow_temperature_c: float | None = None
    board_temperature_c: float | None = None

    accel_x_derivative: float | None = None
    accel_y_derivative: float | None = None
    accel_z_derivative: float | None = None

    microphone_level_derivative: float | None = None

    # Retained for backwards compatibility.
    # The frontend will no longer display it.
    battery_voltage: float | None = None

    activity: str | None = None
    confidence: float | None = None

    packet_type: str | None = None
    capture_id: int | None = None

    gps_satellites: int | None = None
    gps_fix_quality: int | None = None

    # Complete original Heltec JSON.
    telemetry: dict[str, Any] | None = None


class SensorReadingResponse(SensorReadingCreate):
    model_config = ConfigDict(from_attributes=True)

    id: int
    created_at: datetime
