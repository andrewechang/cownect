import { useEffect, useState } from 'react'
import { getReadings } from '../services/api'

const HISTORY_LIMIT = 1000

function hasValidPosition(reading) {
  return (
    Number.isFinite(Number(reading.latitude)) &&
    Number.isFinite(Number(reading.longitude)) &&
    !(Number(reading.latitude) === 0 &&
      Number(reading.longitude) === 0)
  )
}

export function useCowReadings() {
  const [cows, setCows] = useState([])
  const [histories, setHistories] = useState({})
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)

  async function loadReadings() {
    try {
      // Gets the readings used to construct the map history.
      const readings = await getReadings({
        limit: HISTORY_LIMIT,
      })

      const readingsByCollar = new Map()

      for (const reading of readings) {
        const collarId = String(reading.collar_id)

        if (!readingsByCollar.has(collarId)) {
          readingsByCollar.set(collarId, [])
        }

        if (hasValidPosition(reading)) {
          readingsByCollar.get(collarId).push(reading)
        }
      }

      const latestCows = []
      const historyByCollar = {}

      for (const [collarId, collarReadings] of readingsByCollar) {
        const sortedReadings = collarReadings.sort(
          (a, b) =>
            new Date(a.recorded_at) -
            new Date(b.recorded_at)
        )

        if (sortedReadings.length === 0) {
          continue
        }

        // Ordered oldest to newest for drawing the trail.
        historyByCollar[collarId] = sortedReadings

        // The newest reading is the current position.
        latestCows.push(
          sortedReadings[sortedReadings.length - 1]
        )
      }

      setCows(latestCows)
      setHistories(historyByCollar)
      setError(null)
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    loadReadings()

    const interval = setInterval(loadReadings, 10000)

    return () => clearInterval(interval)
  }, [])

  return {
    cows,
    histories,
    loading,
    error,
    reload: loadReadings,
  }
}