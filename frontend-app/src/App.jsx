
import { useEffect, useState } from 'react'

import CowMap from './components/cowMap'
import TelemetryDetails from './components/TelemetryDetails'

import {
  useCowReadings,
  isCollarActive,
} from './hooks/useCowReadings'

import { getCollarHistory } from './services/api'
import './App.css'

function formatTime(value) {
  if (!value) return 'N/A'

  const date = new Date(value)

  if (Number.isNaN(date.getTime())) return 'N/A'

  return date.toLocaleString()
}

function timeAgo(value, now) {
  if (!value) return 'Never'

  const timestamp = Date.parse(value)
  if (!Number.isFinite(timestamp)) return 'Unknown'

  const seconds = Math.max(
    0,
    Math.floor((now - timestamp) / 1000)
  )

  if (seconds < 60) return `${seconds}s ago`

  const minutes = Math.floor(seconds / 60)

  if (minutes < 60) return `${minutes}m ago`

  const hours = Math.floor(minutes / 60)

  if (hours < 24) return `${hours}h ago`

  return `${Math.floor(hours / 24)}d ago`
}

function formatTemperature(value) {
  if (value === null || value === undefined) {
    return 'N/A'
  }

  return `${Number(value).toFixed(1)}°C`
}

function App() {
  const {
    cows,
    histories,
    loading,
    error,
    reload,
  } = useCowReadings()

  const [selectedId, setSelectedId] = useState(null)
  const [sidebarOpen, setSidebarOpen] = useState(true)

  const [history, setHistory] = useState([])
  const [historyLoading, setHistoryLoading] =
    useState(false)
  const [historyError, setHistoryError] = useState(null)

  const [telemetryReading, setTelemetryReading] =
    useState(null)

  // Recalculate connection status even if no new
  // telemetry arrives.
  const [now, setNow] = useState(Date.now())

  useEffect(() => {
    const interval = setInterval(() => {
      setNow(Date.now())
    }, 1000)

    return () => clearInterval(interval)
  }, [])

  const selectedCow = cows.find(
    (cow) => String(cow.collar_id) === selectedId
  ) ?? null

  const activeCount = cows.filter(
    (cow) => isCollarActive(cow, now)
  ).length

  const totalCattle = cows.length

  // No alert engine exists in the current backend.
  const alertCount = 0

  function selectCow(cow) {
    setSelectedId(String(cow.collar_id))
    setTelemetryReading(null)
  }

  function closeDetails() {
    setSelectedId(null)
    setTelemetryReading(null)
  }

  useEffect(() => {
    if (!selectedId) {
      setHistory([])
      return
    }

    let cancelled = false

    async function loadHistory() {
      setHistoryLoading(true)
      setHistoryError(null)

      try {
        const readings = await getCollarHistory(
          selectedId,
          10
        )

        if (!cancelled) {
          setHistory(readings)
        }
      } catch (err) {
        if (!cancelled) {
          setHistoryError(err.message)
        }
      } finally {
        if (!cancelled) {
          setHistoryLoading(false)
        }
      }
    }

    loadHistory()

    return () => {
      cancelled = true
    }
  }, [selectedId])

  return (
    <main className="app-shell">
      <header className="app-header">
        <div>
          <h1>CowNect</h1>
          <p>Real-time cattle monitoring</p>
        </div>

        <button
          className="refresh-button"
          onClick={reload}
        >
          Refresh
        </button>
      </header>

      <section className="summary-row">
        <div className="summary-card">
          <span>Total Cattle</span>
          <strong>{totalCattle}</strong>
        </div>

        <div className="summary-card">
          <span>Connected</span>
          <strong>{activeCount}</strong>
          <small>Transmitted within 10 minutes</small>
        </div>

        <div className="summary-card">
          <span>Alerts</span>
          <strong>{alertCount}</strong>
          <small>Alert rules not configured</small>
        </div>
      </section>

      <section className="dashboard">
        {sidebarOpen && (
          <aside className="sidebar">
            <div className="sidebar-heading">
              <div>
                <h2>Collars</h2>
                <small>
                  {activeCount} active collars
                </small>
              </div>

              <button
                className="close-button"
                onClick={() => setSidebarOpen(false)}
                aria-label="Hide sidebar"
              >
                ×
              </button>
            </div>

            {loading && <p>Loading readings...</p>}

            {error && (
              <p className="error">{error}</p>
            )}

            {cows.map((cow) => {
              const active = isCollarActive(cow, now)

              return (
                <button
                  key={cow.collar_id}
                  className={
                    'cow-list-item ' +
                    (selectedId === String(cow.collar_id)
                      ? 'selected'
                      : '')
                  }
                  onClick={() => selectCow(cow)}
                >
                  <div className="cow-list-heading">
                    <strong>
                      Collar {cow.collar_id}
                    </strong>

                    <span
                      className={
                        'status-dot ' +
                        (active ? 'online' : 'offline')
                      }
                      title={active ? 'Connected' : 'Offline'}
                    />
                  </div>

                  <div className="cow-list-data">
                    <span>
                      Temperature:{' '}
                      {formatTemperature(
                        cow.cow_temperature_c
                      )}
                    </span>

                    <span>
                      Activity:{' '}
                      {cow.activity ?? 'Unknown'}
                    </span>

                    <span>
                      Last transmission:{' '}
                      {timeAgo(cow.created_at, now)}
                    </span>
                  </div>
                </button>
              )
            })}
          </aside>
        )}

        <section className="map-section">
          <button
            className="sidebar-toggle"
            onClick={() =>
              setSidebarOpen((previous) => !previous)
            }
          >
            {sidebarOpen
              ? 'Hide Side Panel'
              : 'Show Side Panel'}
          </button>

          <CowMap
            cows={cows}
            histories={histories}
            selectedCow={selectedCow}
            onSelectCow={selectCow}
            onClearSelection={closeDetails}
            isActive={(cow) =>
              isCollarActive(cow, now)
            }
          />

          {selectedCow && (
            <div
              className="details-card"
              onClick={(event) =>
                event.stopPropagation()
              }
            >
              <div className="details-heading">
                <h2>
                  Collar {selectedCow.collar_id}
                </h2>

                <button
                  className="close-button"
                  onClick={closeDetails}
                  aria-label="Close collar details"
                >
                  ×
                </button>
              </div>

              <div className="current-reading">
                <h3>Current Reading</h3>

                <p>
                  <strong>Status:</strong>{' '}
                  {isCollarActive(selectedCow, now)
                    ? 'Connected'
                    : 'Offline'}
                </p>

                <p>
                  <strong>Cow Temperature:</strong>{' '}
                  {formatTemperature(
                    selectedCow.cow_temperature_c
                  )}
                </p>

                <p>
                  <strong>Board Temperature:</strong>{' '}
                  {formatTemperature(
                    selectedCow.board_temperature_c
                  )}
                </p>

                <p>
                  <strong>Activity:</strong>{' '}
                  {selectedCow.activity ?? 'Unknown'}
                </p>

                <p>
                  <strong>Last Transmission:</strong>{' '}
                  {formatTime(selectedCow.created_at)}
                </p>

                <p>
                  <strong>Latitude:</strong>{' '}
                  {selectedCow.latitude ?? 'N/A'}
                </p>

                <p>
                  <strong>Longitude:</strong>{' '}
                  {selectedCow.longitude ?? 'N/A'}
                </p>
              </div>

              <div className="history-section">
                <h3>Last 10 Readings</h3>

                {historyLoading && (
                  <p>Loading history...</p>
                )}

                {historyError && (
                  <p className="error">{historyError}</p>
                )}

                {!historyLoading &&
                  !historyError &&
                  history.length === 0 && (
                    <p>No history available.</p>
                  )}

                {!historyLoading &&
                  !historyError &&
                  history.length > 0 && (
                    <div className="history-table-wrapper">
                      <table className="history-table">
                        <thead>
                          <tr>
                            <th>Received</th>
                            <th>Packet</th>
                            <th>Cow °C</th>
                            <th>Activity</th>
                            <th>Details</th>
                          </tr>
                        </thead>

                        <tbody>
                          {history.map((reading) => (
                            <tr key={reading.id}>
                              <td>
                                {formatTime(
                                  reading.created_at
                                )}
                              </td>

                              <td>
                                {reading.packet_type ?? 'N/A'}
                              </td>

                              <td>
                                {reading.cow_temperature_c ??
                                  'N/A'}
                              </td>

                              <td>
                                {reading.activity ?? 'Unknown'}
                              </td>

                              <td>
                                <button
                                  className="telemetry-button"
                                  onClick={() =>
                                    setTelemetryReading(reading)
                                  }
                                >
                                  Telemetry
                                </button>
                              </td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                  )}
              </div>
            </div>
          )}
        </section>
      </section>

      {telemetryReading && (
        <TelemetryDetails
          reading={telemetryReading}
          onClose={() => setTelemetryReading(null)}
        />
      )}
    </main>
  )
}

export default App
