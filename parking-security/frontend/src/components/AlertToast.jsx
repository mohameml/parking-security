import { useState, useEffect, useCallback } from 'react'
import { AlertTriangle, UserCheck, Clock, UserX, X, Volume2, VolumeX } from 'lucide-react'

const ALERT_CONFIG = {
  unknown: {
    icon: UserX,
    title: 'UNKNOWN PERSON',
    bg: 'bg-red-600',
    border: 'border-red-500',
    glow: 'shadow-red-500/50',
    text: 'text-red-100',
  },
  wrong_time: {
    icon: Clock,
    title: 'WRONG TIME',
    bg: 'bg-orange-600',
    border: 'border-orange-400',
    glow: 'shadow-orange-500/50',
    text: 'text-orange-100',
  },
  wrong_day: {
    icon: AlertTriangle,
    title: 'WRONG DAY',
    bg: 'bg-orange-700',
    border: 'border-orange-500',
    glow: 'shadow-orange-500/50',
    text: 'text-orange-100',
  },
  authorized: {
    icon: UserCheck,
    title: 'AUTHORIZED',
    bg: 'bg-green-600',
    border: 'border-green-400',
    glow: 'shadow-green-500/30',
    text: 'text-green-100',
  },
}

export function AlertToastContainer({ toasts, onDismiss }) {
  return (
    <div className="fixed top-4 right-4 z-50 flex flex-col gap-3 max-w-sm w-full pointer-events-none">
      {toasts.map((toast) => (
        <AlertToast key={toast.id} toast={toast} onDismiss={onDismiss} />
      ))}
    </div>
  )
}

function AlertToast({ toast, onDismiss }) {
  const [visible, setVisible] = useState(false)
  const [exiting, setExiting] = useState(false)

  const config = ALERT_CONFIG[toast.event_type] || ALERT_CONFIG.unknown
  const Icon = config.icon

  useEffect(() => {
    // Slide in
    requestAnimationFrame(() => setVisible(true))

    // Auto dismiss after 8 seconds (unknown stays longer)
    const duration = toast.event_type === 'unknown' ? 12000 : 6000
    const timer = setTimeout(() => {
      setExiting(true)
      setTimeout(() => onDismiss(toast.id), 300)
    }, duration)

    return () => clearTimeout(timer)
  }, [toast.id, toast.event_type, onDismiss])

  const handleDismiss = () => {
    setExiting(true)
    setTimeout(() => onDismiss(toast.id), 300)
  }

  return (
    <div
      className={`pointer-events-auto transform transition-all duration-300 ease-out ${
        visible && !exiting ? 'translate-x-0 opacity-100' : 'translate-x-full opacity-0'
      }`}
    >
      <div className={`rounded-xl border-2 ${config.border} ${config.bg} shadow-2xl ${config.glow} overflow-hidden`}>
        {/* Header */}
        <div className="flex items-center justify-between px-4 py-2.5">
          <div className="flex items-center gap-2">
            <div className="p-1 bg-white/20 rounded-lg">
              <Icon className="w-4 h-4 text-white" />
            </div>
            <span className="text-sm font-bold text-white tracking-wide">{config.title}</span>
          </div>
          <button onClick={handleDismiss} className="text-white/60 hover:text-white">
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Body */}
        <div className="px-4 pb-3 flex gap-3">
          {/* Face crop */}
          {toast.face_image && (
            <div className="w-14 h-14 rounded-lg overflow-hidden border-2 border-white/30 flex-shrink-0">
              <img
                src={`data:image/jpeg;base64,${toast.face_image}`}
                alt="Detected face"
                className="w-full h-full object-cover"
              />
            </div>
          )}
          <div className="flex-1 min-w-0">
            <p className="text-sm font-semibold text-white truncate">
              {toast.employee_name || 'Unknown Person'}
            </p>
            <p className={`text-xs ${config.text} mt-0.5`}>
              Camera: {toast.camera_id}
            </p>
            <p className={`text-xs ${config.text}`}>
              {new Date(toast.timestamp).toLocaleTimeString()}
              {toast.confidence != null && ` | Confidence: ${(toast.confidence * 100).toFixed(0)}%`}
            </p>
          </div>
        </div>

        {/* Progress bar */}
        <div className="h-1 bg-white/10">
          <div
            className="h-full bg-white/40 rounded-full"
            style={{
              animation: `shrink ${toast.event_type === 'unknown' ? 12 : 6}s linear forwards`,
            }}
          />
        </div>
      </div>
    </div>
  )
}

// Mute toggle button
export function MuteButton({ muted, onToggle }) {
  return (
    <button
      onClick={onToggle}
      className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm transition-colors ${
        muted
          ? 'bg-red-900/50 text-red-400 hover:bg-red-900/70'
          : 'bg-gray-700 text-gray-300 hover:bg-gray-600'
      }`}
      title={muted ? 'Unmute alerts' : 'Mute alerts'}
    >
      {muted ? <VolumeX className="w-4 h-4" /> : <Volume2 className="w-4 h-4" />}
      {muted ? 'Muted' : 'Sound On'}
    </button>
  )
}
