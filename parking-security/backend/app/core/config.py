from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    DATABASE_URL: str = "postgresql+asyncpg://parking:changeme_parking_2025@localhost:5432/parking_security"
    SECRET_KEY: str = "changeme_super_secret_key_min_32_chars!!"
    ALGORITHM: str = "HS256"
    ACCESS_TOKEN_EXPIRE_MINUTES: int = 60
    REFRESH_TOKEN_EXPIRE_DAYS: int = 7

    ADMIN_EMAIL: str = "admin@parking.local"
    ADMIN_PASSWORD: str = "admin123"

    CAMERA_API_KEY: str = "changeme_camera_api_key_2025"

    TELEGRAM_BOT_TOKEN: str = ""
    TELEGRAM_CHAT_ID: str = ""

    SMTP_HOST: str = "smtp.gmail.com"
    SMTP_PORT: int = 587
    SMTP_USER: str = ""
    SMTP_PASSWORD: str = ""
    ALERT_EMAIL_TO: str = ""

    FIREBASE_CREDENTIALS_PATH: str = ""
    FIREBASE_TOPIC: str = "parking-alerts"

    class Config:
        env_file = ".env"


settings = Settings()
