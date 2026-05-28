import { useEffect, useState } from 'react'
import { registerSW } from 'virtual:pwa-register'
import { RefreshCw, X } from 'lucide-react'

export default function UpdatePrompt() {
  const [showUpdate, setShowUpdate] = useState(false)
  const [updateFn, setUpdateFn] = useState(null)

  useEffect(() => {
    const updateSW = registerSW({
      onNeedRefresh() {
        setShowUpdate(true)
        setUpdateFn(() => updateSW)
      },
      onOfflineReady() {
        console.log('App ready to work offline')
      },
    })
  }, [])

  if (!showUpdate) return null

  const handleUpdate = async () => {
    if (updateFn) await updateFn(true)
  }

  return (
    <div className="fixed bottom-4 right-4 z-50 max-w-sm">
      <div className="bg-green-600 rounded-xl p-4 shadow-2xl shadow-green-500/40 border border-green-400 flex items-start gap-3">
        <div className="p-2 bg-white/20 rounded-lg flex-shrink-0">
          <RefreshCw className="w-5 h-5 text-white" />
        </div>
        <div className="flex-1 min-w-0">
          <h3 className="text-sm font-bold text-white">Update Available</h3>
          <p className="text-xs text-green-100 mt-0.5 mb-3">
            A new version of the dashboard is ready.
          </p>
          <button
            onClick={handleUpdate}
            className="bg-white text-green-700 px-3 py-1.5 rounded-lg text-xs font-semibold hover:bg-green-50 transition-colors"
          >
            Reload Now
          </button>
        </div>
        <button
          onClick={() => setShowUpdate(false)}
          className="text-white/60 hover:text-white flex-shrink-0"
        >
          <X className="w-4 h-4" />
        </button>
      </div>
    </div>
  )
}
