import { useEffect, useState } from 'react'
import CowMap from './components/cowMap'
import { useCowReadings } from './hooks/useCowReadings'
import { getCollarHistory } from './services/api'
import './App.css'

function App() {
  const { cows, loading, error, reload } = useCowReadings()

  const [selectedCow, setSelectedCow] = useState(null)
  const [history, setHistory] = useState([])
  const [historyLoading, setHistoryLoading] = useState(false)
  const [historyError, setHistoryError] = useState(null)

  useEffect(() => {
    if (!selectedCow) {
      setHistory([])
      return
    }

    let cancelled = false

    async function loadHistory() {
      setHistoryLoading(true)
      setHistoryError(null)

      try {
        const readings = await getCollarHistory(
          selectedCow.collar_id,
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
  }, [selectedCow])

  return (
    <main className="app-shell">
      <header className="app-header">
        <div>
          <h1>CowNect</h1>
          <p>Real-time cattle monitoring</p>
        </div>

        <button onClick={reload}>Refresh</button>
      </header>

      <section className="dashboard">
        <aside className="sidebar">
          <h2>Cows</h2>
          <p>{cows.length} active collars</p>

          {loading && <p>Loading readings...</p>}
          {error && <p className="error">{error}</p>}

          {cows.map((cow) => (
            <button
              className="cow-list-item"
              key={cow.collar_id}
              onClick={() => setSelectedCow(cow)}
            >
              <span>Collar {cow.collar_id}</span>
              <small>
                {cow.activity ?? 'Unknown activity'}
              </small>
            </button>
          ))}
        </aside>

        <section className="map-section">
          <CowMap
            cows={cows}
            onSelectCow={setSelectedCow}
          />

          {selectedCow && (
            <div className="details-card">
              <h2>Collar {selectedCow.collar_id}</h2>

              <div className="current-reading">
                <h3>Current Reading</h3>

                <p>
                  Activity:{' '}
                  {selectedCow.activity ?? 'Unknown'}
                </p>

                <p>
                  Cow temperature:{' '}
                  {selectedCow.cow_temperature_c ?? 'N/A'} °C
                </p>

                <p>
                  Board temperature:{' '}
                  {selectedCow.board_temperature_c ?? 'N/A'} °C
                </p>

                <p>
                  Battery:{' '}
                  {selectedCow.battery_voltage ?? 'N/A'} V
                </p>

                <p>
                  Last updated:{' '}
                  {new Date(
                    selectedCow.recorded_at
                  ).toLocaleString()}
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
                            <th>Time</th>
                            <th>Cow °C</th>
                            <th>Board °C</th>
                            <th>Latitude</th>
                            <th>Longitude</th>
                          </tr>
                        </thead>

                        <tbody>
                          {history.map((reading) => (
                            <tr key={reading.id}>
                              <td>
                                {new Date(
                                  reading.recorded_at
                                ).toLocaleString()}
                              </td>

                              <td>
                                {reading.cow_temperature_c ??
                                  'N/A'}
                              </td>

                              <td>
                                {reading.board_temperature_c ??
                                  'N/A'}
                              </td>

                              <td>
                                {reading.latitude ?? 'N/A'}
                              </td>

                              <td>
                                {reading.longitude ?? 'N/A'}
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
    </main>
  )
}

export default App