import { useEffect } from 'react'
import { registerSW } from 'virtual:pwa-register'

// Service-worker registration with FULLY AUTOMATIC updates.
//
// vite.config.js sets `registerType: 'autoUpdate'`, which builds a worker that
// calls skipWaiting()/clientsClaim() so a new version activates immediately.
// The key: we must NOT pass an `onNeedRefresh` callback — doing so flips
// vite-plugin-pwa into manual "prompt" mode and SUPPRESSES the auto-reload,
// which is exactly why redeploys used to require manually clearing the SW.
//
// With no onNeedRefresh, registerSW reloads the page automatically once the
// new worker takes control. We also poll for updates every 60s so a
// long-open tab still picks up a redeploy without the user reopening it.
export default function UpdatePrompt() {
  useEffect(() => {
    registerSW({
      immediate: true,
      onRegisteredSW(_swUrl, registration) {
        if (registration) {
          setInterval(() => registration.update(), 60 * 1000)
        }
      },
      onOfflineReady() {
        console.log('[pwa] app ready to work offline')
      },
    })
  }, [])

  return null
}
