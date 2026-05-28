import { useEffect, useRef, useState, useCallback } from 'react'
import useAuthStore from '../stores/authStore'

export default function useWebSocket() {
  const [connected, setConnected] = useState(false)
  const [lastEvent, setLastEvent] = useState(null)
  const [liveFrame, setLiveFrame] = useState(null)
  const wsRef = useRef(null)
  const reconnectTimer = useRef(null)
  const accessToken = useAuthStore((s) => s.accessToken)

  const connect = useCallback(() => {
    if (!accessToken) return

    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws'
    const host = window.location.host
    const ws = new WebSocket(`${protocol}://${host}/ws?token=${accessToken}`)

    ws.onopen = () => {
      setConnected(true)
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current)
    }

    ws.onmessage = (e) => {
      try {
        const data = JSON.parse(e.data)
        if (data.type === 'frame') {
          setLiveFrame(data.frame)
        } else if (data.type === 'detection') {
          setLastEvent(data.event)

          // Browser notification for unknown detections
          if (data.event.event_type === 'unknown' && Notification.permission === 'granted') {
            new Notification('Unknown Person Detected', {
              body: `Camera: ${data.event.camera_id} at ${new Date(data.event.timestamp).toLocaleTimeString()}`,
              icon: data.event.face_image ? `data:image/jpeg;base64,${data.event.face_image}` : undefined,
            })
          }
        }
      } catch {}
    }

    ws.onclose = () => {
      setConnected(false)
      reconnectTimer.current = setTimeout(connect, 3000)
    }

    ws.onerror = () => ws.close()

    wsRef.current = ws
  }, [accessToken])

  useEffect(() => {
    connect()
    return () => {
      if (wsRef.current) wsRef.current.close()
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current)
    }
  }, [connect])

  return { connected, lastEvent, liveFrame }
}
