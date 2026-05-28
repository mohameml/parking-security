import { Outlet, NavLink, useNavigate } from 'react-router-dom'
import { Shield, LayoutDashboard, List, Users, BarChart3, UserCog, LogOut, Wifi, WifiOff, Bell } from 'lucide-react'
import useAuthStore from '../stores/authStore'
import useWebSocket from '../hooks/useWebSocket'
import useEventFeed from '../hooks/useEventFeed'
import useAlerts from '../hooks/useAlerts'
import { AlertToastContainer, MuteButton } from './AlertToast'
import { createContext, useState, useEffect, useCallback, useRef } from 'react'

export const WsContext = createContext({ connected: false, events: [], lastEvent: null, liveFrame: null })

const navItems = [
  { to: '/', icon: LayoutDashboard, label: 'Dashboard' },
  { to: '/events', icon: List, label: 'Events' },
  { to: '/employees', icon: Users, label: 'People', adminOnly: true },
  { to: '/analytics', icon: BarChart3, label: 'Analytics' },
  { to: '/users', icon: UserCog, label: 'Users', adminOnly: true },
]

export default function Layout() {
  const { fullName, role, logout } = useAuthStore()
  const navigate = useNavigate()
  const { connected, lastEvent, liveFrame } = useWebSocket()
  const events = useEventFeed(lastEvent)
  const { triggerAlert } = useAlerts()

  // ── Toast state ──
  const [toasts, setToasts] = useState([])
  const [muted, setMuted] = useState(false)
  const [flashClass, setFlashClass] = useState('')
  const lastEventId = useRef(null)

  // Alert counts
  const unknownCount = events.filter((e) => e.event_type === 'unknown').length
  const violationCount = events.filter((e) => e.event_type === 'wrong_time' || e.event_type === 'wrong_day').length
  const alertCount = unknownCount + violationCount

  // Process new events
  useEffect(() => {
    if (!lastEvent || lastEvent.id === lastEventId.current) return
    lastEventId.current = lastEvent.id

    const type = lastEvent.event_type

    // Add toast for non-authorized events (and authorized too, briefly)
    const toast = { ...lastEvent, id: lastEvent.id || Date.now() }
    setToasts((prev) => [toast, ...prev].slice(0, 5))

    // Sound + browser notification (unless muted)
    if (!muted) {
      triggerAlert(lastEvent)
    }

    // Screen flash for critical alerts
    if (type === 'unknown') {
      setFlashClass('alert-flash-red')
      setTimeout(() => setFlashClass(''), 3000)
    } else if (type === 'wrong_time' || type === 'wrong_day') {
      setFlashClass('alert-flash-orange')
      setTimeout(() => setFlashClass(''), 3000)
    }
  }, [lastEvent, muted, triggerAlert])

  const dismissToast = useCallback((id) => {
    setToasts((prev) => prev.filter((t) => t.id !== id))
  }, [])

  const handleLogout = () => {
    logout()
    navigate('/login')
  }

  return (
    <WsContext.Provider value={{ connected, events, lastEvent, liveFrame }}>
      <div className={`flex h-screen bg-gray-900 ${flashClass}`}>
        {/* Toast Notifications */}
        <AlertToastContainer toasts={toasts} onDismiss={dismissToast} />

        {/* Sidebar */}
        <aside className="w-64 bg-gray-800 border-r border-gray-700 flex flex-col">
          <div className="p-4 border-b border-gray-700">
            <div className="flex items-center gap-2">
              <Shield className="w-8 h-8 text-blue-500" />
              <div>
                <h1 className="text-lg font-bold">Parking Security</h1>
                <p className="text-xs text-gray-400">Face Recognition System</p>
              </div>
            </div>
          </div>

          <nav className="flex-1 p-3 space-y-1">
            {navItems
              .filter((item) => !item.adminOnly || role === 'admin')
              .map((item) => (
                <NavLink
                  key={item.to}
                  to={item.to}
                  end={item.to === '/'}
                  className={({ isActive }) =>
                    `flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm transition-colors ${
                      isActive ? 'bg-blue-600 text-white' : 'text-gray-300 hover:bg-gray-700'
                    }`
                  }
                >
                  <item.icon className="w-5 h-5" />
                  {item.label}
                </NavLink>
              ))}
          </nav>

          {/* Alert Summary */}
          {alertCount > 0 && (
            <div className="mx-3 mb-2 p-3 rounded-lg bg-red-900/30 border border-red-800/50">
              <div className="flex items-center gap-2 mb-1">
                <Bell className="w-4 h-4 text-red-400" />
                <span className="text-sm font-semibold text-red-400">Active Alerts</span>
              </div>
              <div className="flex gap-3 text-xs">
                {unknownCount > 0 && (
                  <span className="text-red-300">{unknownCount} unknown</span>
                )}
                {violationCount > 0 && (
                  <span className="text-orange-300">{violationCount} violations</span>
                )}
              </div>
            </div>
          )}

          <div className="p-4 border-t border-gray-700">
            {/* Connection + Sound Controls */}
            <div className="flex items-center justify-between mb-3">
              <div className="flex items-center gap-2">
                {connected ? (
                  <Wifi className="w-4 h-4 text-green-400" />
                ) : (
                  <WifiOff className="w-4 h-4 text-red-400" />
                )}
                <span className={`text-xs ${connected ? 'text-green-400' : 'text-red-400'}`}>
                  {connected ? 'Live' : 'Disconnected'}
                </span>
              </div>
              <MuteButton muted={muted} onToggle={() => setMuted(!muted)} />
            </div>

            {/* User */}
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium">{fullName}</p>
                <p className="text-xs text-gray-400 capitalize">{role}</p>
              </div>
              <button onClick={handleLogout} className="text-gray-400 hover:text-white">
                <LogOut className="w-5 h-5" />
              </button>
            </div>
          </div>
        </aside>

        {/* Main content */}
        <main className="flex-1 overflow-auto p-6">
          <Outlet />
        </main>
      </div>
    </WsContext.Provider>
  )
}
