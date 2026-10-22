import { useEffect, useState } from 'react'
import { getReadings } from '../services/api'

export function useCowReadings() {
  const [cows, setCows] = useState([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)

  async function loadReadings() {
    try {
      const readings = await getReadings()

      const latestByCollar = new Map()

      for (const reading of readings) {
        const existing = latestByCollar.get(reading.collar_id)

        if (
          !existing ||
          new Date(reading.recorded_at) > new Date(existing.recorded_at)
        ) {
          latestByCollar.set(reading.collar_id, reading)
        }
      }

      const validCows = Array.from(latestByCollar.values()).filter(
        (reading) =>
          reading.latitude !== null &&
          reading.longitude !== null
      )

      setCows(validCows)
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

  return { cows, loading, error, reload: loadReadings }
}