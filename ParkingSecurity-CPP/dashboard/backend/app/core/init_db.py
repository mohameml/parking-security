from sqlalchemy import select

from app.core.config import settings
from app.core.database import Base, engine, async_session
from app.core.security import hash_password
from app.models import User, Camera


async def init_db():
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)

    async with async_session() as session:
        # Seed admin user
        result = await session.execute(select(User).where(User.email == settings.ADMIN_EMAIL))
        admin = result.scalar_one_or_none()
        if not admin:
            admin = User(
                email=settings.ADMIN_EMAIL,
                full_name="System Admin",
                hashed_password=hash_password(settings.ADMIN_PASSWORD),
                role="admin",
                is_active=True,
            )
            session.add(admin)

        # Seed default camera
        result = await session.execute(select(Camera).where(Camera.id == "cam-entrance-01"))
        cam = result.scalar_one_or_none()
        if not cam:
            cam = Camera(
                id="cam-entrance-01",
                name="Main Entrance Camera",
                location="Parking lot entrance gate",
                is_active=True,
            )
            session.add(cam)

        await session.commit()
