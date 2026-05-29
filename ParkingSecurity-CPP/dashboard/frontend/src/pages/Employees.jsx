import { useState, useEffect, useCallback, useRef } from 'react'
import { Users, Plus, Search, X, Upload, Trash2, Edit2, ChevronLeft, ChevronRight, Loader2 } from 'lucide-react'
import api from '../lib/api'

const PAGE_SIZE = 25

export default function Employees() {
  const [employees, setEmployees] = useState([])
  const [studentsPage, setStudentsPage] = useState({ items: [], total: 0, page: 1, pages: 0 })
  const [loading, setLoading] = useState(true)
  const [loadingStudents, setLoadingStudents] = useState(false)
  const [search, setSearch] = useState('')
  const [searchDebounced, setSearchDebounced] = useState('')
  const [showModal, setShowModal] = useState(false)
  const [editingEmployee, setEditingEmployee] = useState(null)
  const [deleteConfirm, setDeleteConfirm] = useState(null)
  const [typeFilter, setTypeFilter] = useState('all')
  const [currentPage, setCurrentPage] = useState(1)

  // Debounce search (400ms)
  const searchTimer = useRef(null)
  useEffect(() => {
    if (searchTimer.current) clearTimeout(searchTimer.current)
    searchTimer.current = setTimeout(() => {
      setSearchDebounced(search)
      setCurrentPage(1)
    }, 400)
    return () => clearTimeout(searchTimer.current)
  }, [search])

  // Fetch employees (tiny list, load once)
  useEffect(() => {
    api.get('/employees', { params: { search: searchDebounced || undefined } })
      .then((r) => setEmployees(r.data.map((e) => ({ ...e, type: 'employee' }))))
      .catch(() => {})
      .finally(() => setLoading(false))
  }, [searchDebounced])

  // Fetch students (paginated)
  const fetchStudents = useCallback(async () => {
    if (typeFilter === 'employee') return
    setLoadingStudents(true)
    try {
      const res = await api.get('/schedule/students', {
        params: {
          search: searchDebounced || undefined,
          page: currentPage,
          page_size: PAGE_SIZE,
        },
      })
      const data = res.data
      setStudentsPage({
        items: (data.items || []).map((s) => ({ ...s, type: 'student' })),
        total: data.total || 0,
        page: data.page || 1,
        pages: data.pages || 0,
      })
    } catch (err) {
      console.error('Failed to fetch students:', err)
    } finally {
      setLoadingStudents(false)
    }
  }, [searchDebounced, currentPage, typeFilter])

  useEffect(() => {
    fetchStudents()
  }, [fetchStudents])

  // Reset page when filter changes
  useEffect(() => {
    setCurrentPage(1)
  }, [typeFilter])

  // Build display list based on filter
  const displayList =
    typeFilter === 'employee'
      ? employees
      : typeFilter === 'student'
        ? studentsPage.items
        : [...employees, ...studentsPage.items]

  const handleDelete = async (id) => {
    try {
      await api.delete(`/employees/${id}`)
      setDeleteConfirm(null)
      setEmployees((prev) => prev.filter((e) => e.id !== id))
    } catch (err) {
      alert(err.response?.data?.detail || 'Delete failed')
    }
  }

  const refetchEmployees = () => {
    api.get('/employees', { params: { search: searchDebounced || undefined } })
      .then((r) => setEmployees(r.data.map((e) => ({ ...e, type: 'employee' }))))
  }

  const totalStudentsMatching = studentsPage.total
  const showingStudents = typeFilter !== 'employee' ? studentsPage.items.length : 0

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <div className="flex items-center gap-2">
          <Users className="w-6 h-6 text-blue-400" />
          <h1 className="text-2xl font-bold">People</h1>
          <span className="text-sm text-gray-400">
            ({employees.length} employees, {totalStudentsMatching} students)
          </span>
        </div>
        <button
          onClick={() => { setEditingEmployee(null); setShowModal(true) }}
          className="flex items-center gap-2 bg-blue-600 hover:bg-blue-700 px-4 py-2 rounded-lg text-sm font-medium transition-colors"
        >
          <Plus className="w-4 h-4" /> Add Employee
        </button>
      </div>

      <div className="flex gap-3 items-center">
        <div className="relative flex-1">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-5 h-5 text-gray-400" />
          <input
            type="text"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            placeholder="Search by name or ID..."
            className="w-full bg-gray-800 border border-gray-700 rounded-lg pl-10 pr-4 py-2.5 text-white focus:outline-none focus:border-blue-500"
          />
          {search !== searchDebounced && (
            <Loader2 className="absolute right-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400 animate-spin" />
          )}
        </div>
        <div className="flex gap-1">
          {[
            { key: 'all', label: 'All' },
            { key: 'employee', label: 'Employees' },
            { key: 'student', label: 'Students' },
          ].map((f) => (
            <button
              key={f.key}
              onClick={() => setTypeFilter(f.key)}
              className={`px-3 py-2 rounded-lg text-sm transition-colors ${
                typeFilter === f.key ? 'bg-blue-600 text-white' : 'bg-gray-700 text-gray-300 hover:bg-gray-600'
              }`}
            >
              {f.label}
            </button>
          ))}
        </div>
      </div>

      <div className="bg-gray-800 rounded-xl overflow-hidden">
        {loading ? (
          <div className="p-8 text-center text-gray-400">Loading...</div>
        ) : displayList.length === 0 ? (
          <div className="p-8 text-center text-gray-400">No people found</div>
        ) : (
          <table className="w-full">
            <thead>
              <tr className="border-b border-gray-700">
                <th className="text-left p-4 text-sm text-gray-400 font-medium">Person</th>
                <th className="text-left p-4 text-sm text-gray-400 font-medium">ID</th>
                <th className="text-left p-4 text-sm text-gray-400 font-medium">Type</th>
                <th className="text-left p-4 text-sm text-gray-400 font-medium">Info</th>
                <th className="text-left p-4 text-sm text-gray-400 font-medium">Status</th>
                <th className="text-left p-4 text-sm text-gray-400 font-medium">Face</th>
                <th className="text-right p-4 text-sm text-gray-400 font-medium">Actions</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-700">
              {displayList.map((emp) => (
                <tr key={`${emp.type}-${emp.id}`} className="hover:bg-gray-700/50 transition-colors">
                  <td className="p-4">
                    <div className="flex items-center gap-3">
                      <PersonAvatar name={emp.full_name || emp.employee_number || '?'} photo={emp.photo_path} />
                      <div>
                        <p className="font-medium">{emp.full_name || emp.employee_number}</p>
                        <p className="text-xs text-gray-400">{emp.email || (emp.type === 'student' ? 'Student' : 'No email')}</p>
                      </div>
                    </div>
                  </td>
                  <td className="p-4 text-sm text-gray-300">{emp.employee_number}</td>
                  <td className="p-4">
                    <span className={`text-xs px-2 py-0.5 rounded-full font-medium ${
                      emp.type === 'student' ? 'bg-blue-500/20 text-blue-400' : 'bg-purple-500/20 text-purple-400'
                    }`}>
                      {emp.type === 'student' ? 'Student' : 'Employee'}
                    </span>
                  </td>
                  <td className="p-4 text-sm text-gray-300">
                    {emp.type === 'student' ? (
                      <span>{emp.exam_count || 0} exams</span>
                    ) : (
                      <span>{emp.department || '-'}</span>
                    )}
                  </td>
                  <td className="p-4">
                    <span className={`text-xs px-2 py-0.5 rounded-full ${emp.is_active ? 'bg-green-500/20 text-green-400' : 'bg-gray-500/20 text-gray-400'}`}>
                      {emp.is_active ? 'Active' : 'Inactive'}
                    </span>
                  </td>
                  <td className="p-4 text-sm text-gray-300">
                    {emp.embedding_count > 0 ? (
                      <span className="text-green-400">{emp.embedding_count} enrolled</span>
                    ) : (
                      <span className="text-red-400">None</span>
                    )}
                  </td>
                  <td className="p-4 text-right">
                    {emp.type === 'employee' ? (
                      <div className="flex items-center justify-end gap-2">
                        <button
                          onClick={() => { setEditingEmployee(emp); setShowModal(true) }}
                          className="p-1.5 text-gray-400 hover:text-blue-400 transition-colors"
                        >
                          <Edit2 className="w-4 h-4" />
                        </button>
                        <button
                          onClick={() => setDeleteConfirm(emp.id)}
                          className="p-1.5 text-gray-400 hover:text-red-400 transition-colors"
                        >
                          <Trash2 className="w-4 h-4" />
                        </button>
                      </div>
                    ) : (
                      <span className="text-xs text-gray-500">Imported</span>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}

        {/* Pagination controls for students */}
        {typeFilter !== 'employee' && studentsPage.pages > 1 && (
          <div className="border-t border-gray-700 p-4 flex items-center justify-between">
            <p className="text-sm text-gray-400">
              Showing {(currentPage - 1) * PAGE_SIZE + 1}-{Math.min(currentPage * PAGE_SIZE, totalStudentsMatching)} of {totalStudentsMatching} students
              {loadingStudents && <Loader2 className="inline-block ml-2 w-4 h-4 animate-spin" />}
            </p>
            <Pagination
              page={currentPage}
              pages={studentsPage.pages}
              onPageChange={(p) => setCurrentPage(p)}
            />
          </div>
        )}
      </div>

      {/* Delete Confirmation */}
      {deleteConfirm && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
          <div className="bg-gray-800 rounded-xl p-6 max-w-sm mx-4">
            <h3 className="text-lg font-bold mb-2">Confirm Delete</h3>
            <p className="text-gray-400 mb-4">This will permanently remove the employee and all face embeddings. This action cannot be undone.</p>
            <div className="flex justify-end gap-3">
              <button onClick={() => setDeleteConfirm(null)} className="px-4 py-2 bg-gray-700 rounded-lg text-sm">Cancel</button>
              <button onClick={() => handleDelete(deleteConfirm)} className="px-4 py-2 bg-red-600 rounded-lg text-sm">Delete</button>
            </div>
          </div>
        </div>
      )}

      {showModal && (
        <EmployeeModal
          employee={editingEmployee}
          onClose={() => { setShowModal(false); setEditingEmployee(null) }}
          onSaved={refetchEmployees}
        />
      )}
    </div>
  )
}

function Pagination({ page, pages, onPageChange }) {
  // Show max 5 page numbers around current
  const windowSize = 2
  const start = Math.max(1, page - windowSize)
  const end = Math.min(pages, page + windowSize)
  const visibleNumbers = []
  for (let i = start; i <= end; i++) visibleNumbers.push(i)

  return (
    <div className="flex items-center gap-1">
      <button
        onClick={() => onPageChange(page - 1)}
        disabled={page === 1}
        className="p-1.5 rounded-lg bg-gray-700 hover:bg-gray-600 disabled:opacity-40 disabled:cursor-not-allowed"
      >
        <ChevronLeft className="w-4 h-4" />
      </button>

      {start > 1 && (
        <>
          <PageBtn page={1} current={page} onClick={onPageChange} />
          {start > 2 && <span className="text-gray-500 px-1">...</span>}
        </>
      )}

      {visibleNumbers.map((n) => (
        <PageBtn key={n} page={n} current={page} onClick={onPageChange} />
      ))}

      {end < pages && (
        <>
          {end < pages - 1 && <span className="text-gray-500 px-1">...</span>}
          <PageBtn page={pages} current={page} onClick={onPageChange} />
        </>
      )}

      <button
        onClick={() => onPageChange(page + 1)}
        disabled={page === pages}
        className="p-1.5 rounded-lg bg-gray-700 hover:bg-gray-600 disabled:opacity-40 disabled:cursor-not-allowed"
      >
        <ChevronRight className="w-4 h-4" />
      </button>
    </div>
  )
}

function PageBtn({ page, current, onClick }) {
  return (
    <button
      onClick={() => onClick(page)}
      className={`min-w-[32px] px-2 py-1 rounded-lg text-sm ${
        current === page ? 'bg-blue-600 text-white' : 'bg-gray-700 text-gray-300 hover:bg-gray-600'
      }`}
    >
      {page}
    </button>
  )
}

function PersonAvatar({ name, photo }) {
  const initial = (name || '?').charAt(0).toUpperCase()

  return (
    <div className="w-10 h-10 rounded-full bg-gray-600 overflow-hidden flex-shrink-0">
      {photo ? (
        <img src={photo} alt={name} className="w-full h-full object-cover" loading="lazy" />
      ) : (
        <div className="w-full h-full flex items-center justify-center text-gray-400 text-sm font-medium">
          {initial}
        </div>
      )}
    </div>
  )
}

function EmployeeModal({ employee, onClose, onSaved }) {
  const [form, setForm] = useState({
    employee_number: employee?.employee_number || '',
    full_name: employee?.full_name || '',
    department: employee?.department || '',
    email: employee?.email || '',
    phone: employee?.phone || '',
    is_active: employee?.is_active ?? true,
  })
  const [photo, setPhoto] = useState(null)
  const [dragOver, setDragOver] = useState(false)
  const [saving, setSaving] = useState(false)
  const [error, setError] = useState('')

  const handleSubmit = async (e) => {
    e.preventDefault()
    setSaving(true)
    setError('')

    try {
      const formData = new FormData()
      formData.append('employee_number', form.employee_number)
      formData.append('full_name', form.full_name)
      formData.append('department', form.department)
      formData.append('email', form.email)
      formData.append('phone', form.phone)

      if (employee) {
        formData.append('is_active', String(form.is_active))
      }
      if (photo) {
        formData.append('photo', photo)
      }

      if (employee) {
        await api.put(`/employees/${employee.id}`, formData, {
          headers: { 'Content-Type': 'multipart/form-data' },
        })
      } else {
        await api.post('/employees', formData, {
          headers: { 'Content-Type': 'multipart/form-data' },
        })
      }

      onSaved()
      onClose()
    } catch (err) {
      setError(err.response?.data?.detail || 'Save failed')
    } finally {
      setSaving(false)
    }
  }

  const handleDrop = (e) => {
    e.preventDefault()
    setDragOver(false)
    const file = e.dataTransfer.files[0]
    if (file && (file.type === 'image/jpeg' || file.type === 'image/png')) {
      setPhoto(file)
    }
  }

  return (
    <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div className="bg-gray-800 rounded-xl p-6 w-full max-w-lg mx-4 max-h-[90vh] overflow-y-auto">
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-lg font-bold">{employee ? 'Edit Employee' : 'Add Employee'}</h3>
          <button onClick={onClose} className="text-gray-400 hover:text-white"><X className="w-5 h-5" /></button>
        </div>

        <form onSubmit={handleSubmit} className="space-y-4">
          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="block text-sm text-gray-300 mb-1">Full Name *</label>
              <input
                value={form.full_name}
                onChange={(e) => setForm({ ...form, full_name: e.target.value })}
                className="w-full bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-white text-sm"
                required
              />
            </div>
            <div>
              <label className="block text-sm text-gray-300 mb-1">Employee Number *</label>
              <input
                value={form.employee_number}
                onChange={(e) => setForm({ ...form, employee_number: e.target.value })}
                className="w-full bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-white text-sm"
                required
                disabled={!!employee}
              />
            </div>
          </div>

          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="block text-sm text-gray-300 mb-1">Department</label>
              <input
                value={form.department}
                onChange={(e) => setForm({ ...form, department: e.target.value })}
                className="w-full bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-white text-sm"
              />
            </div>
            <div>
              <label className="block text-sm text-gray-300 mb-1">Email</label>
              <input
                type="email"
                value={form.email}
                onChange={(e) => setForm({ ...form, email: e.target.value })}
                className="w-full bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-white text-sm"
              />
            </div>
          </div>

          <div>
            <label className="block text-sm text-gray-300 mb-1">Phone</label>
            <input
              value={form.phone}
              onChange={(e) => setForm({ ...form, phone: e.target.value })}
              className="w-full bg-gray-700 border border-gray-600 rounded-lg px-3 py-2 text-white text-sm"
            />
          </div>

          {employee && (
            <div className="flex items-center gap-2">
              <input
                type="checkbox"
                checked={form.is_active}
                onChange={(e) => setForm({ ...form, is_active: e.target.checked })}
                className="rounded"
                id="active-toggle"
              />
              <label htmlFor="active-toggle" className="text-sm text-gray-300">Active employee</label>
            </div>
          )}

          <div>
            <label className="block text-sm text-gray-300 mb-1">Face Photo {!employee && '(JPEG/PNG)'}</label>
            <div
              onDrop={handleDrop}
              onDragOver={(e) => { e.preventDefault(); setDragOver(true) }}
              onDragLeave={() => setDragOver(false)}
              className={`border-2 border-dashed rounded-lg p-6 text-center transition-colors ${
                dragOver ? 'border-blue-500 bg-blue-900/20' : 'border-gray-600'
              }`}
            >
              {photo ? (
                <div className="flex items-center justify-center gap-2">
                  <span className="text-sm text-green-400">{photo.name}</span>
                  <button type="button" onClick={() => setPhoto(null)} className="text-red-400"><X className="w-4 h-4" /></button>
                </div>
              ) : (
                <div>
                  <Upload className="w-8 h-8 text-gray-400 mx-auto mb-2" />
                  <p className="text-sm text-gray-400">Drag & drop a photo or</p>
                  <label className="text-sm text-blue-400 cursor-pointer hover:underline">
                    browse files
                    <input
                      type="file"
                      accept="image/jpeg,image/png"
                      className="hidden"
                      onChange={(e) => setPhoto(e.target.files[0])}
                    />
                  </label>
                </div>
              )}
            </div>
          </div>

          {error && <p className="text-red-400 text-sm">{error}</p>}

          <div className="flex justify-end gap-3 pt-2">
            <button type="button" onClick={onClose} className="px-4 py-2 bg-gray-700 rounded-lg text-sm">Cancel</button>
            <button type="submit" disabled={saving} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 rounded-lg text-sm font-medium">
              {saving ? 'Saving...' : employee ? 'Update' : 'Create'}
            </button>
          </div>
        </form>
      </div>
    </div>
  )
}
