"""Generate PWA icons for the Parking Security System."""
from PIL import Image, ImageDraw
import os

os.makedirs("public", exist_ok=True)


def draw_shield(size, bg_color=(15, 23, 42), shield_color=(59, 130, 246), padding_ratio=0.0):
    """Draw a shield icon with a face-like indicator.

    padding_ratio: 0 for regular icon, 0.1 for maskable (safe zone).
    """
    img = Image.new("RGBA", (size, size), bg_color)
    draw = ImageDraw.Draw(img)

    # Safe zone for maskable icons -- draw inside the central 80% area
    pad = int(size * padding_ratio)
    inner_size = size - 2 * pad

    # Shield shape (simplified as a rounded shape)
    cx = size // 2
    top = pad + int(inner_size * 0.15)
    bottom = size - pad - int(inner_size * 0.10)
    left = pad + int(inner_size * 0.18)
    right = size - pad - int(inner_size * 0.18)

    # Draw shield body as a rounded rectangle with pointed bottom
    shield_points = [
        (left, top),
        (right, top),
        (right, top + int(inner_size * 0.50)),
        (cx, bottom),
        (left, top + int(inner_size * 0.50)),
    ]
    draw.polygon(shield_points, fill=shield_color)

    # Draw a face circle inside the shield (white)
    face_radius = int(inner_size * 0.12)
    face_cy = top + int(inner_size * 0.27)
    draw.ellipse(
        [cx - face_radius, face_cy - face_radius, cx + face_radius, face_cy + face_radius],
        fill=(255, 255, 255),
    )

    # Draw body/neck (white arc)
    body_top = face_cy + face_radius - 2
    body_bottom = top + int(inner_size * 0.52)
    body_width = int(inner_size * 0.30)
    draw.ellipse(
        [cx - body_width // 2, body_top, cx + body_width // 2, body_bottom + body_width // 2],
        fill=(255, 255, 255),
    )

    # Clip the body with a rectangle
    mask = Image.new("L", (size, size), 0)
    mask_draw = ImageDraw.Draw(mask)
    mask_draw.polygon(shield_points, fill=255)

    # Composite via masked paste
    final = Image.new("RGBA", (size, size), bg_color)
    final.paste(img, (0, 0), mask)

    return final


def create_favicon():
    img = draw_shield(64)
    img.save("public/favicon.svg".replace(".svg", ".png"))  # PNG is fine
    # Save as ICO too
    img.save("public/favicon.ico", format="ICO", sizes=[(32, 32), (64, 64)])


# Generate icons
draw_shield(192).save("public/icon-192.png")
draw_shield(512).save("public/icon-512.png")
draw_shield(512, padding_ratio=0.12).save("public/icon-512-maskable.png")

# Favicon
draw_shield(64).save("public/favicon.png")
draw_shield(32).save("public/favicon.ico", format="ICO", sizes=[(16, 16), (32, 32)])

# Apple touch icon
draw_shield(180).save("public/apple-touch-icon.png")

print("Icons generated successfully in public/")
for f in sorted(os.listdir("public")):
    path = os.path.join("public", f)
    print(f"  {f}: {os.path.getsize(path)} bytes")
