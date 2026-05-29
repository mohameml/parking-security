import { useState, useEffect } from 'react'
import { List, ChevronDown, ChevronUp, X, Clock, Camera, Shield, AlertTriangle, UserCheck, UserX, ZoomIn, Trash2 } from 'lucide-react'
import api from '../lib/api'

const timeRanges = [
  { label: 'Last hour', value: 1 },
  { label: 'Last 6 hours', value: 6 },
  { label: 'Last 24 hours', value: 24 },
  { label: 'Last 7 days', value: 168 },
]

const EVENT_STYLES = {
  authorized: { border: 'border-green-500', badge: 'bg-green-500/20 text-green-400', label: 'AUTHORIZED', icon: UserCheck, color: 'green' },
  wrong_time: { border: 'border-orange-500', badge: 'bg-orange-500/20 text-orange-400', label: 'WRONG TIME', icon: Clock, color: 'orange' },
  wrong_day: { border: 'border-orange-600', badge: 'bg-orange-600/20 text-orange-400', label: 'WRONG DAY', icon: AlertTriangle, color: 'orange' },
  unknown: { border: 'border-red-500', badge: 'bg-red-500/20 text-red-400', label: 'UNKNOWN', icon: UserX, color: 'red' },
  known: { border: 'border-green-500', badge: 'bg-green-500/20 text-green-400', label: 'KNOWN', icon: UserCheck, color: 'green' },
}

export default function Events() {
  const [events, setEvents] = useState([])
  const [loading, setLoading] = useState(true)
  const [hours, setHours] = useState(24)
  const [typeFilter, setTypeFilter] = useState('')
  const [expandedId, setExpandedId] = useState(null)
  const [lightbox, setLightbox] = useState(null) // { type: 'face'|'frame', src, event }

  useEffect(() => {
    fetchEvents()
  }, [hours, typeFilter])

  const fetchEvents = async () => {
    setLoading(true)
    try {
      const params = { hours, limit: 200 }
      if (typeFilter) params.event_type = typeFilter
      const res = await api.get('/events', { params })
      setEvents(res.data)
    } catch (err) {
      console.error('Failed to fetch events:', err)
    } finally {
      setLoading(false)
    }
  }

  const [showClearConfirm, setShowClearConfirm] = useState(false)

  const clearEvents = async () => {
    try {
      await api.delete('/events/clear')
      setEvents([])
      setShowClearConfirm(false)
    } catch (err) {
      alert(err.response?.data?.detail || 'Failed to clear events')
    }
  }

  const counts = {
    all: events.length,
    authorized: events.filter(e => e.event_type === 'authorized' || e.event_type === 'known').length,
    violation: events.filter(e => e.event_type === 'wrong_time' || e.event_type === 'wrong_day').length,
    unknown: events.filter(e => e.event_type === 'unknown').length,
  }

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between flex-wrap gap-4">
        <div className="flex items-center gap-2">
          <List className="w-6 h-6 text-blue-400" />
          <h1 className="text-2xl font-bold">Event Log</h1>
          <span className="text-sm text-gray-400">({events.length} events)</span>
        </div>

        <div className="flex gap-2">
          <select
            value={hours}
            onChange={(e) => setHours(Number(e.target.value))}
            className="bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-sm text-white"
          >
            {timeRanges.map((r) => (
              <option key={r.value} value={r.value}>{r.label}</option>
            ))}
          </select>

          <select
            value={typeFilter}
            onChange={(e) => setTypeFilter(e.target.value)}
            className="bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-sm text-white"
          >
            <option value="">All types ({counts.all})</option>
            <option value="authorized">Authorized ({counts.authorized})</option>
            <option value="wrong_time">Wrong Time</option>
            <option value="wrong_day">Wrong Day</option>
            <option value="unknown">Unknown ({counts.unknown})</option>
          </select>

          {events.length > 0 && (
            <button
              onClick={() => setShowClearConfirm(true)}
              className="flex items-center gap-1.5 bg-red-900/30 hover:bg-red-900/50 border border-red-800/50 text-red-400 px-3 py-2 rounded-lg text-sm transition-colors"
            >
              <Trash2 className="w-4 h-4" /> Clear All
            </button>
          )}
        </div>
      </div>

      {/* Summary Cards */}
      <div className="grid grid-cols-4 gap-3">
        <MiniCard label="Total" value={counts.all} color="blue" />
        <MiniCard label="Authorized" value={counts.authorized} color="green" />
        <MiniCard label="Violations" value={counts.violation} color="orange" />
        <MiniCard label="Unknown" value={counts.unknown} color="red" />
      </div>

      {/* Events List */}
      <div className="bg-gray-800 rounded-xl overflow-hidden">
        {loading ? (
          <div className="p-8 text-center text-gray-400">Loading events...</div>
        ) : events.length === 0 ? (
          <div className="p-8 text-center text-gray-400">No events found for this time range</div>
        ) : (
          <div className="divide-y divide-gray-700">
            {events.map((ev) => {
              const style = EVENT_STYLES[ev.event_type] || EVENT_STYLES.unknown
              const Icon = style.icon
              const isExpanded = expandedId === ev.id

              return (
                <div key={ev.id}>
                  {/* Event Row */}
                  <div
                    className={`flex items-center gap-4 p-4 cursor-pointer hover:bg-gray-700/30 transition-all border-l-4 ${style.border}`}
                    onClick={() => setExpandedId(isExpanded ? null : ev.id)}
                  >
                    {/* Face Thumbnail */}
                    <div className="relative group">
                      <div className="w-12 h-12 rounded-lg overflow-hidden bg-gray-700 flex-shrink-0">
                        {ev.face_image ? (
                          <img src={`data:image/jpeg;base64,${ev.face_image}`} alt="" className="w-full h-full object-cover" />
                        ) : (
                          <div className="w-full h-full flex items-center justify-center">
                            <Icon className="w-5 h-5 text-gray-500" />
                          </div>
                        )}
                      </div>
                      {ev.face_image && (
                        <button
                          onClick={(e) => { e.stopPropagation(); setLightbox({ type: 'face', src: `data:image/jpeg;base64,${ev.face_image}`, event: ev }) }}
                          className="absolute inset-0 bg-black/0 group-hover:bg-black/40 rounded-lg flex items-center justify-center opacity-0 group-hover:opacity-100 transition-all"
                        >
                          <ZoomIn className="w-4 h-4 text-white" />
                        </button>
                      )}
                    </div>

                    {/* Info */}
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className={`text-xs px-2 py-0.5 rounded-full font-medium ${style.badge}`}>
                          {style.label}
                        </span>
                        <span className="text-sm font-medium truncate">
                          {ev.employee_name || 'Unknown Person'}
                        </span>
                      </div>
                      <p className="text-xs text-gray-400 mt-0.5">
                        {ev.camera_id} &middot; {new Date(ev.timestamp).toLocaleString()}
                        {ev.confidence != null && ` &middot; ${(ev.confidence * 100).toFixed(1)}%`}
                      </p>
                    </div>

                    {/* Expand Icon */}
                    <div className={`transition-transform duration-200 ${isExpanded ? 'rotate-180' : ''}`}>
                      <ChevronDown className="w-5 h-5 text-gray-400" />
                    </div>
                  </div>

                  {/* Expanded Detail Panel */}
                  {isExpanded && (
                    <div className="bg-gray-900/70 border-t border-gray-700 p-5">
                      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5">

                        {/* Face Crop - Large */}
                        <div className="flex flex-col">
                          <p className="text-xs text-gray-400 mb-2 font-medium uppercase tracking-wider">Face Capture</p>
                          {ev.face_image ? (
                            <div
                              className="relative group cursor-pointer rounded-xl overflow-hidden bg-gray-800 border border-gray-700"
                              onClick={() => setLightbox({ type: 'face', src: `data:image/jpeg;base64,${ev.face_image}`, event: ev })}
                            >
                              <img
                                src={`data:image/jpeg;base64,${ev.face_image}`}
                                alt="Face capture"
                                className="w-full max-h-64 object-contain"
                              />
                              <div className="absolute inset-0 bg-black/0 group-hover:bg-black/30 flex items-center justify-center transition-all">
                                <div className="opacity-0 group-hover:opacity-100 bg-black/60 px-3 py-1.5 rounded-lg flex items-center gap-1.5 transition-all">
                                  <ZoomIn className="w-4 h-4 text-white" />
                                  <span className="text-xs text-white font-medium">Click to enlarge</span>
                                </div>
                              </div>
                            </div>
                          ) : (
                            <div className="h-40 rounded-xl bg-gray-800 border border-gray-700 flex items-center justify-center text-gray-500 text-sm">
                              No face image
                            </div>
                          )}
                        </div>

                        {/* Full Frame */}
                        <div className="flex flex-col">
                          <p className="text-xs text-gray-400 mb-2 font-medium uppercase tracking-wider">Full Frame</p>
                          {ev.frame_image ? (
                            <div
                              className="relative group cursor-pointer rounded-xl overflow-hidden bg-gray-800 border border-gray-700"
                              onClick={() => {
                                const src = ev.frame_image.startsWith('/snapshots') ? ev.frame_image : `/snapshots/${ev.frame_image.replace('/snapshots/', '')}`
                                setLightbox({ type: 'frame', src, event: ev })
                              }}
                            >
                              <img
                                src={ev.frame_image.startsWith('/snapshots') ? ev.frame_image : `/snapshots/${ev.frame_image.replace('/snapshots/', '')}`}
                                alt="Full frame"
                                className="w-full max-h-64 object-contain"
                              />
                              <div className="absolute inset-0 bg-black/0 group-hover:bg-black/30 flex items-center justify-center transition-all">
                                <div className="opacity-0 group-hover:opacity-100 bg-black/60 px-3 py-1.5 rounded-lg flex items-center gap-1.5 transition-all">
                                  <ZoomIn className="w-4 h-4 text-white" />
                                  <span className="text-xs text-white font-medium">Click to enlarge</span>
                                </div>
                              </div>
                            </div>
                          ) : (
                            <div className="h-40 rounded-xl bg-gray-800 border border-gray-700 flex items-center justify-center text-gray-500 text-sm">
                              No frame capture
                            </div>
                          )}
                        </div>

                        {/* Event Details */}
                        <div className="flex flex-col">
                          <p className="text-xs text-gray-400 mb-2 font-medium uppercase tracking-wider">Details</p>
                          <div className="rounded-xl bg-gray-800 border border-gray-700 p-4 space-y-3 flex-1">
                            <DetailRow icon={Shield} label="Event Type" value={style.label} valueClass={style.badge} />
                            <DetailRow icon={UserCheck} label="Person" value={ev.employee_name || 'Unknown'} />
                            <DetailRow icon={Camera} label="Camera" value={ev.camera_id} />
                            <DetailRow icon={Clock} label="Time" value={new Date(ev.timestamp).toLocaleString()} />
                            {ev.confidence != null && (
                              <div>
                                <p className="text-xs text-gray-400 mb-1">Confidence</p>
                                <div className="flex items-center gap-2">
                                  <div className="flex-1 h-2 bg-gray-700 rounded-full overflow-hidden">
                                    <div
                                      className={`h-full rounded-full ${
                                        ev.confidence > 0.6 ? 'bg-green-500' : ev.confidence > 0.4 ? 'bg-orange-500' : 'bg-red-500'
                                      }`}
                                      style={{ width: `${Math.max(ev.confidence * 100, 5)}%` }}
                                    />
                                  </div>
                                  <span className="text-sm font-mono">{(ev.confidence * 100).toFixed(1)}%</span>
                                </div>
                              </div>
                            )}
                            <DetailRow icon={AlertTriangle} label="Alert Sent" value={ev.alert_sent ? 'Yes' : 'No'} />
                            <p className="text-xs text-gray-500 pt-1 border-t border-gray-700 font-mono break-all">
                              ID: {ev.id}
                            </p>
                          </div>
                        </div>
                      </div>
                    </div>
                  )}
                </div>
              )
            })}
          </div>
        )}
      </div>

      {/* Clear Confirmation */}
      {showClearConfirm && (
        <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50">
          <div className="bg-gray-800 rounded-xl p-6 max-w-sm mx-4 border border-gray-700">
            <h3 className="text-lg font-bold mb-2">Clear All Events</h3>
            <p className="text-gray-400 text-sm mb-4">This will permanently delete all {events.length} events. This action cannot be undone.</p>
            <div className="flex justify-end gap-3">
              <button onClick={() => setShowClearConfirm(false)} className="px-4 py-2 bg-gray-700 hover:bg-gray-600 rounded-lg text-sm transition-colors">Cancel</button>
              <button onClick={clearEvents} className="px-4 py-2 bg-red-600 hover:bg-red-700 rounded-lg text-sm font-medium transition-colors">Delete All</button>
            </div>
          </div>
        </div>
      )}

      {/* Lightbox Modal */}
      {lightbox && (
        <Lightbox
          src={lightbox.src}
          event={lightbox.event}
          type={lightbox.type}
          onClose={() => setLightbox(null)}
        />
      )}
    </div>
  )
}

function DetailRow({ icon: Icon, label, value, valueClass }) {
  return (
    <div className="flex items-start gap-2">
      <Icon className="w-4 h-4 text-gray-500 mt-0.5 flex-shrink-0" />
      <div>
        <p className="text-xs text-gray-400">{label}</p>
        {valueClass ? (
          <span className={`text-xs px-2 py-0.5 rounded-full font-medium ${valueClass}`}>{value}</span>
        ) : (
          <p className="text-sm text-gray-200">{value}</p>
        )}
      </div>
    </div>
  )
}

function MiniCard({ label, value, color }) {
  const colors = {
    blue: 'bg-blue-900/30 text-blue-400 border-blue-800/50',
    green: 'bg-green-900/30 text-green-400 border-green-800/50',
    orange: 'bg-orange-900/30 text-orange-400 border-orange-800/50',
    red: 'bg-red-900/30 text-red-400 border-red-800/50',
  }
  return (
    <div className={`rounded-lg border p-3 text-center ${colors[color]}`}>
      <p className="text-2xl font-bold">{value}</p>
      <p className="text-xs mt-0.5 opacity-80">{label}</p>
    </div>
  )
}

function Lightbox({ src, event, type, onClose }) {
  const style = EVENT_STYLES[event?.event_type] || EVENT_STYLES.unknown

  return (
    <div
      className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4"
      onClick={onClose}
    >
      <div
        className="relative max-w-4xl w-full max-h-[90vh] flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between mb-3">
          <div className="flex items-center gap-3">
            <span className={`text-xs px-2.5 py-1 rounded-full font-medium ${style.badge}`}>
              {style.label}
            </span>
            <span className="text-white font-medium">{event?.employee_name || 'Unknown Person'}</span>
            <span className="text-gray-400 text-sm">
              {event?.camera_id} &middot; {event?.timestamp ? new Date(event.timestamp).toLocaleString() : ''}
            </span>
          </div>
          <button
            onClick={onClose}
            className="p-2 bg-gray-800 hover:bg-gray-700 rounded-lg transition-colors"
          >
            <X className="w-5 h-5 text-white" />
          </button>
        </div>

        {/* Image */}
        <div className="flex-1 flex items-center justify-center bg-gray-900 rounded-xl overflow-hidden border border-gray-700">
          <img
            src={src}
            alt={type === 'face' ? 'Face capture' : 'Full frame'}
            className="max-w-full max-h-[80vh] object-contain"
          />
        </div>

        {/* Footer */}
        <div className="flex items-center justify-between mt-3 text-sm text-gray-400">
          <span>{type === 'face' ? 'Face Capture' : 'Full Frame Snapshot'}</span>
          {event?.confidence != null && (
            <span>Confidence: {(event.confidence * 100).toFixed(1)}%</span>
          )}
        </div>
      </div>
    </div>
  )
}
