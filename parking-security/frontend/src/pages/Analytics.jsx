import { useState, useEffect } from 'react'
import { BarChart3, TrendingUp, AlertTriangle, UserCheck, Eye } from 'lucide-react'
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts'
import api from '../lib/api'

export default function Analytics() {
  const [days, setDays] = useState(7)
  const [summary, setSummary] = useState(null)
  const [daily, setDaily] = useState([])
  const [hourly, setHourly] = useState([])
  const [topEmployees, setTopEmployees] = useState([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    fetchAll()
  }, [days])

  const fetchAll = async () => {
    setLoading(true)
    try {
      const [sumRes, dailyRes, hourlyRes, topRes] = await Promise.all([
        api.get('/analytics/summary', { params: { days } }),
        api.get('/analytics/daily', { params: { days } }),
        api.get('/analytics/hourly', { params: { days } }),
        api.get('/analytics/top-employees', { params: { days } }),
      ])
      setSummary(sumRes.data)
      setDaily(dailyRes.data)
      setHourly(hourlyRes.data)
      setTopEmployees(topRes.data)
    } catch (err) {
      console.error('Failed to fetch analytics:', err)
    } finally {
      setLoading(false)
    }
  }

  // Fill hourly data with all 24 hours
  const hourlyFull = Array.from({ length: 24 }, (_, i) => {
    const found = hourly.find((h) => h.hour === i)
    return { hour: `${String(i).padStart(2, '0')}:00`, count: found?.count || 0 }
  })

  const peakHour = hourlyFull.reduce((max, h) => (h.count > max.count ? h : max), { count: 0 })

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <div className="flex items-center gap-2">
          <BarChart3 className="w-6 h-6 text-blue-400" />
          <h1 className="text-2xl font-bold">Analytics</h1>
        </div>
        <div className="flex gap-1">
          {[7, 14, 30].map((d) => (
            <button
              key={d}
              onClick={() => setDays(d)}
              className={`px-3 py-1.5 rounded-lg text-sm transition-colors ${
                days === d ? 'bg-blue-600 text-white' : 'bg-gray-700 text-gray-300 hover:bg-gray-600'
              }`}
            >
              {d} days
            </button>
          ))}
        </div>
      </div>

      {loading ? (
        <div className="text-center text-gray-400 py-12">Loading analytics...</div>
      ) : (
        <>
          {/* Summary Cards */}
          {summary && (
            <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
              <KPICard icon={Eye} label="Total Detections" value={summary.total} color="blue" />
              <KPICard icon={UserCheck} label="Known" value={summary.known} color="green" />
              <KPICard icon={AlertTriangle} label="Unknown" value={summary.unknown} color="red" />
              <KPICard icon={TrendingUp} label="Unknown Rate" value={`${summary.unknown_rate}%`} color={summary.unknown_rate > 20 ? 'red' : 'blue'} />
            </div>
          )}

          {/* Daily Bar Chart */}
          <div className="bg-gray-800 rounded-xl p-4">
            <h2 className="text-lg font-semibold mb-4">Daily Detections</h2>
            <div className="h-72">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={daily}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
                  <XAxis dataKey="date" tick={{ fill: '#9CA3AF', fontSize: 12 }} />
                  <YAxis tick={{ fill: '#9CA3AF', fontSize: 12 }} />
                  <Tooltip contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: 8 }} />
                  <Legend />
                  <Bar dataKey="known" fill="#22C55E" name="Known" radius={[4, 4, 0, 0]} />
                  <Bar dataKey="unknown" fill="#EF4444" name="Unknown" radius={[4, 4, 0, 0]} />
                </BarChart>
              </ResponsiveContainer>
            </div>
          </div>

          {/* Hourly Heatmap */}
          <div className="bg-gray-800 rounded-xl p-4">
            <h2 className="text-lg font-semibold mb-4">
              Hourly Activity
              {peakHour.count > 0 && (
                <span className="text-sm text-gray-400 ml-2">(Peak: {peakHour.hour})</span>
              )}
            </h2>
            <div className="h-56">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={hourlyFull}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
                  <XAxis dataKey="hour" tick={{ fill: '#9CA3AF', fontSize: 10 }} interval={1} />
                  <YAxis tick={{ fill: '#9CA3AF', fontSize: 12 }} />
                  <Tooltip contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: 8 }} />
                  <Bar
                    dataKey="count"
                    name="Detections"
                    radius={[4, 4, 0, 0]}
                    fill="#3B82F6"
                    // Highlight peak hour
                    shape={(props) => {
                      const isPeak = props.payload.hour === peakHour.hour && peakHour.count > 0
                      return (
                        <rect
                          x={props.x}
                          y={props.y}
                          width={props.width}
                          height={props.height}
                          fill={isPeak ? '#F59E0B' : '#3B82F6'}
                          rx={4}
                        />
                      )
                    }}
                  />
                </BarChart>
              </ResponsiveContainer>
            </div>
          </div>

          {/* Top Employees */}
          <div className="bg-gray-800 rounded-xl p-4">
            <h2 className="text-lg font-semibold mb-4">Top Employees by Visits</h2>
            {topEmployees.length === 0 ? (
              <p className="text-gray-400 text-sm">No data for this period</p>
            ) : (
              <div className="space-y-3">
                {topEmployees.map((emp, i) => {
                  const maxVisits = topEmployees[0]?.visits || 1
                  const pct = (emp.visits / maxVisits) * 100
                  return (
                    <div key={i} className="flex items-center gap-3">
                      <span className="text-sm text-gray-400 w-6 text-right">{i + 1}</span>
                      <div className="flex-1">
                        <div className="flex items-center justify-between mb-1">
                          <span className="text-sm font-medium">{emp.employee_name}</span>
                          <span className="text-sm text-gray-400">{emp.visits} visits</span>
                        </div>
                        <div className="h-2 bg-gray-700 rounded-full overflow-hidden">
                          <div className="h-full bg-blue-500 rounded-full" style={{ width: `${pct}%` }} />
                        </div>
                      </div>
                    </div>
                  )
                })}
              </div>
            )}
          </div>
        </>
      )}
    </div>
  )
}

function KPICard({ icon: Icon, label, value, color }) {
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
