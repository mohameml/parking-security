import { useState, useEffect } from 'react'

export default function useEventFeed(lastEvent) {
  const [events, setEvents] = useState([])

  useEffect(() => {
    if (lastEvent) {
      setEvents((prev) => {
        const updated = [lastEvent, ...prev]
        return updated.slice(0, 100) // Keep latest 100
      })
    }
  }, [lastEvent])

  return events
}
