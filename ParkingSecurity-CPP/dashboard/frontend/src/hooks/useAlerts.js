import { useCallback, useRef } from 'react'

// ─── Web Audio API Alarm Sounds (no files needed) ───

const AudioCtx = window.AudioContext || window.webkitAudioContext

function playTone(frequency, duration, type = 'sine', volume = 0.3) {
  try {
    const ctx = new AudioCtx()
    const osc = ctx.createOscillator()
    const gain = ctx.createGain()
    osc.connect(gain)
    gain.connect(ctx.destination)
    osc.type = type
    osc.frequency.value = frequency
    gain.gain.value = volume
    gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + duration)
    osc.start()
    osc.stop(ctx.currentTime + duration)
  } catch {}
}

function playUnknownAlarm() {
  // Urgent double-beep alarm
  playTone(880, 0.15, 'square', 0.25)
  setTimeout(() => playTone(880, 0.15, 'square', 0.25), 200)
  setTimeout(() => playTone(660, 0.3, 'square', 0.2), 500)
}

function playViolationAlarm() {
  // Warning triple-beep
  playTone(600, 0.1, 'triangle', 0.2)
  setTimeout(() => playTone(600, 0.1, 'triangle', 0.2), 150)
  setTimeout(() => playTone(600, 0.1, 'triangle', 0.2), 300)
}

function playAuthorizedSound() {
  // Soft positive chime
  playTone(523, 0.1, 'sine', 0.1)
  setTimeout(() => playTone(659, 0.1, 'sine', 0.1), 100)
  setTimeout(() => playTone(784, 0.15, 'sine', 0.1), 200)
}

// ─── Alert Hook ───

export default function useAlerts() {
  const lastAlertTime = useRef(0)

  const triggerAlert = useCallback((event) => {
    if (!event) return

    // Throttle: max 1 sound per 2 seconds
    const now = Date.now()
    if (now - lastAlertTime.current < 2000) return
    lastAlertTime.current = now

    const type = event.event_type

    if (type === 'unknown') {
      playUnknownAlarm()
    } else if (type === 'wrong_time' || type === 'wrong_day') {
      playViolationAlarm()
    } else if (type === 'authorized') {
      playAuthorizedSound()
    }

    // Browser notification (works when tab is background)
    if (type !== 'authorized' && Notification.permission === 'granted') {
      const title = type === 'unknown'
        ? 'UNKNOWN PERSON DETECTED'
        : type === 'wrong_time'
          ? 'WRONG TIME - Schedule Violation'
          : 'WRONG DAY - Schedule Violation'

      const body = `Camera: ${event.camera_id}\nTime: ${new Date(event.timestamp).toLocaleTimeString()}\n${event.employee_name || 'Unknown person'}`

      new Notification(title, {
        body,
        icon: event.face_image ? `data:image/jpeg;base64,${event.face_image}` : undefined,
        tag: `alert-${event.id || Date.now()}`,
        requireInteraction: type === 'unknown',
      })
    }
  }, [])

  return { triggerAlert }
}
