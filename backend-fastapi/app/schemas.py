from datetime import datetime

#pydantic is a data validation library to validate data before sending to API
from pydantic import BaseModel, ConfigDict

class SensorReadingCreate(BaseModel):
    #two required values
    collar_id: str
    recorded_at: datetime

    #optional values, if the Jetson doesn't send them then they will show up as "None"
    latitude: float | None = None
    longitude: float | None = None
    cow_temperature_c: float | None = None
    board_temperature_c: float | None = None
    accel_x_derivative: float | None = None
    accel_y_derivative: float | None = None
    accel_z_derivative: float | None = None
    microphone_level_derivative: float | None = None
    battery_voltage: float | None = None
    activity: str | None = None
    confidence: float | None = None


#API returns a JSON that includes values sent to it
class SensorReadingResponse(SensorReadingCreate):
    # Allow Pydantic to read values from a SQLAlchemy object.
    model_config = ConfigDict(from_attributes=True)

    # PostgreSQL generates these values, so they only appear in responses.
    id: int
    created_at: datetime