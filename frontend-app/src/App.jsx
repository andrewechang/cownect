import { useState } from 'react'
import CowMap from './components/CowMap'
import { useCowReadings } from './hooks/useCowReadings'
import './App.css'

function App() {
  const { cows, loading, error, reload } = useCowReadings()
  const [selectedCow, setSelectedCow] = useState(null)

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
              <span>{cow.collar_id}</span>
              <small>{cow.activity ?? 'Unknown activity'}</small>
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
              <h2>{selectedCow.collar_id}</h2>
              <p>Activity: {selectedCow.activity ?? 'Unknown'}</p>
              <p>
                Cow temperature:{' '}
                {selectedCow.cow_temperature_c ?? 'N/A'} °C
              </p>
              <p>
                Battery:{' '}
                {selectedCow.battery_voltage ?? 'N/A'} V
              </p>
              <p>
                Last updated:{' '}
                {new Date(selectedCow.recorded_at).toLocaleString()}
              </p>
            </div>
          )}
        </section>
      </section>
    </main>
  )
}

export default App