-- parking-security database schema. Compatible with the Python prototype
-- so both projects can share a database during the migration.

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- ─── Auth ──────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    email VARCHAR(255) UNIQUE NOT NULL,
    full_name VARCHAR(255) NOT NULL,
    hashed_password VARCHAR(255) NOT NULL,
    role VARCHAR(20) NOT NULL DEFAULT 'guard',
    is_active BOOLEAN DEFAULT true,
    failed_login_attempts INT DEFAULT 0,
    locked_until TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT now(),
    updated_at TIMESTAMPTZ DEFAULT now()
);

-- ─── Employees ─────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS employees (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    employee_number VARCHAR(50) UNIQUE NOT NULL,
    full_name VARCHAR(255) NOT NULL,
    department VARCHAR(100),
    email VARCHAR(255),
    phone VARCHAR(50),
    photo_path VARCHAR(500),
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMPTZ DEFAULT now(),
    updated_at TIMESTAMPTZ DEFAULT now()
);

CREATE TABLE IF NOT EXISTS face_embeddings (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    employee_id UUID NOT NULL REFERENCES employees(id) ON DELETE CASCADE,
    embedding REAL[] NOT NULL,       -- 512-dim vector
    created_at TIMESTAMPTZ DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_face_embeddings_employee ON face_embeddings(employee_id);

-- ─── Students + Schedule ───────────────────────────────────
CREATE TABLE IF NOT EXISTS students (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    student_id VARCHAR(20) UNIQUE NOT NULL,
    full_name VARCHAR(255),
    photo_path VARCHAR(500),
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMPTZ DEFAULT now()
);

CREATE TABLE IF NOT EXISTS student_embeddings (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    student_id UUID NOT NULL REFERENCES students(id) ON DELETE CASCADE,
    embedding REAL[] NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_student_embeddings_student ON student_embeddings(student_id);

CREATE TABLE IF NOT EXISTS exam_schedules (
    id INTEGER PRIMARY KEY,
    room VARCHAR(100) NOT NULL,
    room_ar VARCHAR(100),
    center VARCHAR(200),
    subject_code VARCHAR(50),
    subject VARCHAR(300) NOT NULL,
    time_slot VARCHAR(10) NOT NULL,   -- H1 | H2
    start_time TIME NOT NULL,
    end_time TIME NOT NULL,
    exam_date DATE NOT NULL,
    program VARCHAR(50),
    semester VARCHAR(10)
);
CREATE INDEX IF NOT EXISTS idx_exam_date_slot ON exam_schedules(exam_date, time_slot);

CREATE TABLE IF NOT EXISTS student_exams (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    student_id UUID NOT NULL REFERENCES students(id) ON DELETE CASCADE,
    exam_id INTEGER NOT NULL REFERENCES exam_schedules(id) ON DELETE CASCADE,
    seat INTEGER,
    presence BOOLEAN DEFAULT false
);
CREATE INDEX IF NOT EXISTS idx_student_exams_student ON student_exams(student_id);

-- ─── Cameras + Events ──────────────────────────────────────
CREATE TABLE IF NOT EXISTS cameras (
    id VARCHAR(100) PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    location VARCHAR(255),
    is_active BOOLEAN DEFAULT true,
    last_seen TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT now()
);

CREATE TABLE IF NOT EXISTS events (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    camera_id VARCHAR(100) NOT NULL REFERENCES cameras(id),
    event_type VARCHAR(20) NOT NULL,   -- authorized | wrong_time | wrong_day | unknown
    employee_id UUID REFERENCES employees(id),
    employee_name VARCHAR(255),
    confidence REAL,
    bbox VARCHAR(200),
    face_image TEXT,                    -- base64 JPEG
    frame_image VARCHAR(500),           -- path on disk
    alert_sent BOOLEAN DEFAULT false,
    timestamp TIMESTAMPTZ DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_events_type_timestamp ON events(event_type, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_events_camera ON events(camera_id);

-- ─── Audit ─────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS audit_logs (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES users(id),
    user_email VARCHAR(255),
    action VARCHAR(100) NOT NULL,
    resource VARCHAR(100),
    resource_id VARCHAR(255),
    details TEXT,
    ip_address VARCHAR(45),
    timestamp TIMESTAMPTZ DEFAULT now()
);
