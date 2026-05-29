import { create } from 'zustand'
import { persist } from 'zustand/middleware'

const useAuthStore = create(
  persist(
    (set, get) => ({
      accessToken: null,
      refreshToken: null,
      role: null,
      fullName: null,
      isAuthenticated: false,

      login: ({ access_token, refresh_token, role, full_name }) => {
        set({
          accessToken: access_token,
          refreshToken: refresh_token,
          role,
          fullName: full_name,
          isAuthenticated: true,
        })
      },

      updateTokens: ({ access_token, refresh_token }) => {
        set({ accessToken: access_token, refreshToken: refresh_token })
      },

      logout: () => {
        set({
          accessToken: null,
          refreshToken: null,
          role: null,
          fullName: null,
          isAuthenticated: false,
        })
      },

      isAdmin: () => get().role === 'admin',
    }),
    { name: 'parking-auth' }
  )
)

export default useAuthStore
