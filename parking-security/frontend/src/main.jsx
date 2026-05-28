import React from 'react'
import ReactDOM from 'react-dom/client'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import './index.css'
import useAuthStore from './stores/authStore'
import Login from './pages/Login'
import Layout from './components/Layout'
import Dashboard from './pages/Dashboard'
import Events from './pages/Events'
import Employees from './pages/Employees'
import Analytics from './pages/Analytics'
import Users from './pages/Users'
import InstallPrompt from './components/InstallPrompt'
import UpdatePrompt from './components/UpdatePrompt'

function ProtectedRoute({ children, adminOnly = false }) {
  const { isAuthenticated, role } = useAuthStore()
  if (!isAuthenticated) return <Navigate to="/login" />
  if (adminOnly && role !== 'admin') return <Navigate to="/" />
  return children
}

function App() {
  return (
    <BrowserRouter>
      <InstallPrompt />
      <UpdatePrompt />
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route
          path="/"
          element={
            <ProtectedRoute>
              <Layout />
            </ProtectedRoute>
          }
        >
          <Route index element={<Dashboard />} />
          <Route path="events" element={<Events />} />
          <Route
            path="employees"
            element={
              <ProtectedRoute adminOnly>
                <Employees />
              </ProtectedRoute>
            }
          />
          <Route path="analytics" element={<Analytics />} />
          <Route
            path="users"
            element={
              <ProtectedRoute adminOnly>
                <Users />
              </ProtectedRoute>
            }
          />
        </Route>
      </Routes>
    </BrowserRouter>
  )
}

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
)
