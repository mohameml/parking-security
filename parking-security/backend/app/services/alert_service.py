import asyncio
import base64
import logging
from email.mime.image import MIMEImage
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText

import aiohttp
import aiosmtplib

from app.core.config import settings

logger = logging.getLogger(__name__)


async def send_telegram_alert(camera_id: str, timestamp: str, face_b64: str | None, event_id: str):
    if not settings.TELEGRAM_BOT_TOKEN or not settings.TELEGRAM_CHAT_ID:
        logger.info("Telegram not configured, skipping")
        return

    try:
        text = (
            f"🚨 *UNKNOWN PERSON DETECTED*\n\n"
            f"Camera: `{camera_id}`\n"
            f"Time: `{timestamp}`\n"
            f"Event ID: `{event_id}`"
        )

        async with aiohttp.ClientSession() as session:
            if face_b64:
                image_data = base64.b64decode(face_b64)
                form = aiohttp.FormData()
                form.add_field("chat_id", settings.TELEGRAM_CHAT_ID)
                form.add_field("caption", text)
                form.add_field("parse_mode", "Markdown")
                form.add_field("photo", image_data, filename="face.jpg", content_type="image/jpeg")
                url = f"https://api.telegram.org/bot{settings.TELEGRAM_BOT_TOKEN}/sendPhoto"
                async with session.post(url, data=form) as resp:
                    if resp.status != 200:
                        logger.error(f"Telegram photo failed: {await resp.text()}")
            else:
                url = f"https://api.telegram.org/bot{settings.TELEGRAM_BOT_TOKEN}/sendMessage"
                payload = {"chat_id": settings.TELEGRAM_CHAT_ID, "text": text, "parse_mode": "Markdown"}
                async with session.post(url, json=payload) as resp:
                    if resp.status != 200:
                        logger.error(f"Telegram message failed: {await resp.text()}")

        logger.info("Telegram alert sent")
    except Exception as e:
        logger.error(f"Telegram alert error: {e}")


async def send_email_alert(camera_id: str, timestamp: str, face_b64: str | None, event_id: str):
    if not settings.SMTP_USER or not settings.ALERT_EMAIL_TO:
        logger.info("Email not configured, skipping")
        return

    try:
        msg = MIMEMultipart()
        msg["From"] = settings.SMTP_USER
        msg["To"] = settings.ALERT_EMAIL_TO
        msg["Subject"] = f"🚨 Unknown Person Detected - {camera_id}"

        body = (
            f"An unknown person was detected.\n\n"
            f"Camera: {camera_id}\n"
            f"Time: {timestamp}\n"
            f"Event ID: {event_id}\n"
        )
        msg.attach(MIMEText(body, "plain"))

        if face_b64:
            image_data = base64.b64decode(face_b64)
            image = MIMEImage(image_data, name="face.jpg")
            msg.attach(image)

        await aiosmtplib.send(
            msg,
            hostname=settings.SMTP_HOST,
            port=settings.SMTP_PORT,
            username=settings.SMTP_USER,
            password=settings.SMTP_PASSWORD,
            start_tls=True,
        )
        logger.info("Email alert sent")
    except Exception as e:
        logger.error(f"Email alert error: {e}")


async def send_firebase_alert(camera_id: str, timestamp: str, event_id: str):
    if not settings.FIREBASE_CREDENTIALS_PATH:
        logger.info("Firebase not configured, skipping")
        return

    try:
        import firebase_admin
        from firebase_admin import credentials, messaging

        if not firebase_admin._apps:
            cred = credentials.Certificate(settings.FIREBASE_CREDENTIALS_PATH)
            firebase_admin.initialize_app(cred)

        message = messaging.Message(
            topic=settings.FIREBASE_TOPIC,
            notification=messaging.Notification(
                title="Unknown Person Detected",
                body=f"Camera: {camera_id} at {timestamp}",
            ),
            data={"event_id": event_id, "camera_id": camera_id, "timestamp": timestamp},
        )
        messaging.send(message)
        logger.info("Firebase alert sent")
    except Exception as e:
        logger.error(f"Firebase alert error: {e}")


async def fire_all_alerts(camera_id: str, timestamp: str, face_b64: str | None, event_id: str):
    results = await asyncio.gather(
        send_telegram_alert(camera_id, timestamp, face_b64, event_id),
        send_email_alert(camera_id, timestamp, face_b64, event_id),
        send_firebase_alert(camera_id, timestamp, event_id),
        return_exceptions=True,
    )
    for i, result in enumerate(results):
        if isinstance(result, Exception):
            logger.error(f"Alert channel {i} failed: {result}")
