const API_BASE_URL =
  import.meta.env.VITE_API_BASE_URL || 'http://localhost:8000'

export async function getReadings(options = {}) {
  const params = new URLSearchParams()

  if (options.collarId) {
    params.set('collar_id', options.collarId)
  }

  if (options.limit) {
    params.set('limit', options.limit)
  }

  const queryString = params.toString()
  const url = `${API_BASE_URL}/api/v1/readings${
    queryString ? `?${queryString}` : ''
  }`

  const response = await fetch(url)

  if (!response.ok) {
    throw new Error(`API request failed: ${response.status}`)
  }

  return response.json()
}

export async function getCollarHistory(collarId, limit = 10) {
  return getReadings({
    collarId,
    limit,
  })
}