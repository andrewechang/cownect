
import { useEffect, useState } from 'react'
import { getReadings } from '../services/api'

const HISTORY_LIMIT = 1000
export const ACTIVE_TIMEOUT_MS = 10 * 60 * 1000

export function isCollarActive(cow, now = Date.now()) {
  if (!cow?.created_at) return false

  const timestamp = Date.parse(cow.created_at)

  return (
    Number.isFinite(timestamp) &&
    timestamp <= now &&
    now - timestamp < ACTIVE_TIMEOUT_MS
  )
}

function hasValidPosition(reading) {
  if (reading.latitude == null ||
      reading.longitude == null) {
    return false
  }

  const lat = Number(reading.latitude)
  const lng = Number(reading.longitude)

  return (
    Number.isFinite(lat) &&
    Number.isFinite(lng) &&
    !(lat === 0 && lng === 0) &&
    lat >= -90 && lat <= 90 &&
    lng >= -180 && lng <= 180
  )
}

export function useCowReadings() {
  const [cows, setCows] = useState([])
  const [histories, setHistories] = useState({})
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)

  async function loadReadings() {
    try {
      const readings = await getReadings({
        limit: HISTORY_LIMIT,
      })

      const grouped = new Map()

      for (const reading of readings) {
        const id = String(reading.collar_id)

        if (!grouped.has(id)) {
          grouped.set(id, [])
        }

        grouped.get(id).push(reading)
      }

      const latestCows = []
      const historyByCollar = {}

      for (const [id, rows] of grouped) {
        // Newest database receipt first.
        rows.sort((a, b) =>
          new Date(b.created_at) -
          new Date(a.created_at) ||
          b.id - a.id
        )

        const latest = rows[0]

        // Use the most recent valid GPS fix for the
        // marker, even if the latest packet lacks GPS.
        const latestPosition = rows.find(
          hasValidPosition
        )

        latestCows.push({
          ...latest,
          latitude: latestPosition?.latitude ?? null,
          longitude: latestPosition?.longitude ?? null,
        })

        // Map trail: oldest position to newest.
        historyByCollar[id] = rows
          .filter(hasValidPosition)
          .reverse()
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

    const interval = setInterval(
      loadReadings,
      10000
    )

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
