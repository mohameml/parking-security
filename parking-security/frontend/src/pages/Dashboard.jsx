import { useContext, useState } from 'react'
import { Camera, AlertTriangle, UserCheck, Eye, Video } from 'lucide-react'
import { WsContext } from '../components/Layout'

// Camera stream URL - direct MJPEG from Jetson camera service
const STREAM_URL = `http://${window.location.hostname}:8080/stream`
const SNAPSHOT_URL = `http://${window.location.hostname}:8080/snapshot`

export default function Dashboard() {
  const { connected, events } = useContext(WsContext)
  const [filter, setFilter] = useState('all')
  const [streamError, setStreamError] = useState(false)

  const filteredEvents = events.filter((e) => {
    if (filter === 'unknown') return e.event_type === 'unknown'
    if (filter === 'authorized') return e.event_type === 'authorized'
    if (filter === 'violation') return e.event_type === 'wrong_time' || e.event_type === 'wrong_day'
    return true
  })

  const unknownCount = events.filter((e) => e.event_type === 'unknown').length
  const authorizedCount = events.filter((e) => e.event_type === 'authorized').length
  const violationCount = events.filter((e) => e.event_type === 'wrong_time' || e.event_type === 'wrong_day').length
  const lastFaces = events.slice(0, 8)

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold">Live Dashboard</h1>
        <div className="flex items-center gap-3">
          <span
            className={`inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-sm ${
              connected ? 'bg-green-900/50 text-green-400' : 'bg-red-900/50 text-red-400'
            }`}
          >
            <span className={`w-2 h-2 rounded-full ${connected ? 'bg-green-400 animate-pulse' : 'bg-red-400'}`} />
            {connected ? 'Events Live' : 'Events Disconnected'}
          </span>
          <span className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-sm bg-blue-900/50 text-blue-400">
            <Video className="w-3.5 h-3.5" />
            MJPEG Stream
          </span>
        </div>
      </div>

      {/* Stat Cards */}
      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <StatCard icon={Eye} label="Total Detections" value={events.length} color="blue" />
        <StatCard icon={UserCheck} label="Authorized" value={authorizedCount} color="green" />
        <StatCard icon={AlertTriangle} label="Violations" value={violationCount} color="red" />
        <StatCard icon={Camera} label="Unknown" value={unknownCount} color="red" />
      </div>

      {/* Live MJPEG Stream */}
      <div className="bg-gray-800 rounded-xl p-4">
        <div className="flex items-center justify-between mb-3">
          <h2 className="text-lg font-semibold">Camera Feed</h2>
          <span className="text-xs text-gray-400">cam-entrance-01 &middot; 1080p &middot; 30 FPS</span>
        </div>
        <div className="aspect-video bg-gray-900 rounded-lg flex items-center justify-center overflow-hidden relative">
          {!streamError ? (
            <img
              src={STREAM_URL}
              alt="Live MJPEG feed"
              className="w-full h-full object-contain"
              onError={() => setStreamError(true)}
            />
          ) : (
            <div className="text-gray-500 text-center">
              <Camera className="w-12 h-12 mx-auto mb-2" />
              <p>Camera stream unavailable</p>
              <p className="text-sm text-gray-600 mt-1">Waiting for camera service on port 8080...</p>
              <button
                onClick={() => setStreamError(false)}
                className="mt-3 px-4 py-1.5 bg-blue-600 hover:bg-blue-700 rounded-lg text-sm text-white"
              >
                Retry Connection
              </button>
            </div>
          )}
          {/* Live indicator overlay */}
          {!streamError && (
            <div className="absolute top-3 right-3 flex items-center gap-1.5 bg-red-600/90 px-2.5 py-1 rounded-md">
              <span className="w-2 h-2 rounded-full bg-white animate-pulse" />
              <span className="text-xs font-bold text-white">LIVE</span>
            </div>
          )}
        </div>
      </div>

      {/* Face Thumbnail Strip */}
      {lastFaces.length > 0 && (
        <div className="bg-gray-800 rounded-xl p-4">
          <h2 className="text-lg font-semibold mb-3">Recent Detections</h2>
          <div className="flex gap-3 overflow-x-auto pb-2">
            {lastFaces.map((ev, i) => (
              <div key={i} className="flex-shrink-0 text-center">
                <div
                  className={`w-16 h-16 rounded-lg overflow-hidden border-2 ${
                    ev.event_type === 'unknown' ? 'border-red-500' :
                    ev.event_type === 'wrong_time' || ev.event_type === 'wrong_day' ? 'border-orange-500' :
                    'border-green-500'
                  }`}
                >
                  {ev.face_image ? (
                    <img src={`data:image/jpeg;base64,${ev.face_image}`} alt="Face" className="w-full h-full object-cover" />
                  ) : (
                    <div className="w-full h-full bg-gray-700 flex items-center justify-center text-xs text-gray-400">N/A</div>
                  )}
                </div>
                <p className="text-xs mt-1 truncate w-16">
                  {ev.event_type === 'known' ? ev.employee_name?.split(' ')[0] : 'Unknown'}
                </p>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Alert Panel */}
      <div className="bg-gray-800 rounded-xl p-4">
        <div className="flex items-center justify-between mb-3">
          <h2 className="text-lg font-semibold">Event Feed</h2>
          <div className="flex gap-1">
            {[
              { key: 'all', label: 'All', count: events.length },
              { key: 'authorized', label: 'Authorized', count: authorizedCount },
              { key: 'violation', label: 'Violations', count: violationCount },
              { key: 'unknown', label: 'Unknown', count: unknownCount },
            ].map((f) => (
              <button
                key={f.key}
                onClick={() => setFilter(f.key)}
                className={`px-3 py-1 rounded-lg text-sm transition-colors ${
                  filter === f.key ? 'bg-blue-600 text-white' : 'bg-gray-700 text-gray-300 hover:bg-gray-600'
                }`}
              >
                {f.label} ({f.count})
              </button>
            ))}
          </div>
        </div>

        <div className="space-y-2 max-h-96 overflow-y-auto">
          {filteredEvents.length === 0 ? (
            <p className="text-gray-500 text-center py-8">No events yet — stand in front of the camera to trigger a detection</p>
          ) : (
            filteredEvents.map((ev, i) => (
              <EventRow key={i} ev={ev} />
            ))
          )}
        </div>
      </div>
    </div>
  )
}

const EVENT_STYLES = {
  authorized: { bg: 'bg-gray-700/50', badge: 'bg-green-500/20 text-green-400', label: 'AUTHORIZED' },
  wrong_time: { bg: 'bg-orange-900/20 border border-orange-800/50', badge: 'bg-orange-500/20 text-orange-400', label: 'WRONG TIME' },
  wrong_day: { bg: 'bg-red-900/20 border border-red-800/50', badge: 'bg-red-500/20 text-red-400', label: 'WRONG DAY' },
  unknown: { bg: 'bg-red-900/30 border border-red-700/50', badge: 'bg-red-600/20 text-red-300', label: 'UNKNOWN' },
  known: { bg: 'bg-gray-700/50', badge: 'bg-green-500/20 text-green-400', label: 'KNOWN' },
}

function EventRow({ ev }) {
  const style = EVENT_STYLES[ev.event_type] || EVENT_STYLES.unknown
  return (
    <div className={`flex items-center gap-3 p-3 rounded-lg ${style.bg}`}>
      <div className="w-10 h-10 rounded overflow-hidden flex-shrink-0">
        {ev.face_image ? (
          <img src={`data:image/jpeg;base64,${ev.face_image}`} alt="" className="w-full h-full object-cover" />
        ) : (
          <div className="w-full h-full bg-gray-600" />
        )}
      </div>
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2">
          <span className={`text-xs px-2 py-0.5 rounded-full font-medium ${style.badge}`}>
            {style.label}
          </span>
          <span className="text-sm font-medium truncate">
            {ev.employee_name || 'Unknown Person'}
          </span>
        </div>
        <p className="text-xs text-gray-400">
          {ev.camera_id} &middot; {new Date(ev.timestamp).toLocaleTimeString()}
          {ev.confidence != null && ` &middot; ${(ev.confidence * 100).toFixed(1)}%`}
        </p>
      </div>
    </div>
  )
}

function StatCard({ icon: Icon, label, value, color }) {
  const colors = {
    blue: 'bg-blue-900/30 text-blue-400',
    green: 'bg-green-900/30 text-green-400',
    red: 'bg-red-900/30 text-red-400',
  }
  return (
    <div className="bg-gray-800 rounded-xl p-4">
      <div className="flex items-center gap-3">
        <div className={`p-2 rounded-lg ${colors[color]}`}>
          <Icon className="w-5 h-5" />
        </div>
        <div>
          <p className="text-sm text-gray-400">{label}</p>
          <p className="text-xl font-bold">{value}</p>
        </div>
      </div>
    </div>
  )
}
