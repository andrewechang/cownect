import { useEffect } from 'react'

import {
  CircleMarker,
  MapContainer,
  Polyline,
  Popup,
  TileLayer,
  useMap,
} from 'react-leaflet'
import 'leaflet/dist/leaflet.css'

function FocusOnCow({ selectedCow }) {
  const map = useMap()

  useEffect(() => {
    if (!selectedCow) {
      return
    }

    const latitude = Number(selectedCow.latitude)
    const longitude = Number(selectedCow.longitude)

    if (!Number.isFinite(latitude) ||
        !Number.isFinite(longitude)) {
      return
    }

    map.flyTo(
      [latitude, longitude],
      17,
      {
        animate: true,
        duration: 1.25,
      }
    )
  }, [map, selectedCow])

  return null
}

function CowMap({
  cows,
  histories,
  selectedCow,
  onSelectCow,
}) {
  const defaultCenter = [33.7175, -117.8311]

  return (
    <MapContainer
      center={defaultCenter}
      zoom={15}
      className="cow-map"
    >

      <FocusOnCow selectedCow={selectedCow} />
      <TileLayer
        attribution="&copy; OpenStreetMap contributors"
        url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
      />

      {cows.map((cow) => {
        const collarId = String(cow.collar_id)

        const history = histories[collarId] ?? [cow]

        const positions = history.map((reading) => [
          Number(reading.latitude),
          Number(reading.longitude),
        ])

        const currentPosition = positions[positions.length - 1]

        return (
          <div key={collarId}>
            {/* Draw each trail segment with increasing opacity toward
                the current position. */}
            {positions.length > 1 &&
              positions.slice(0, -1).map((position, index) => {
                const nextPosition = positions[index + 1]

                const opacity =
                  0.15 +
                  0.75 *
                    ((index + 1) / (positions.length - 1))

                return (
                  <Polyline
                    key={`${collarId}-line-${index}`}
                    positions={[position, nextPosition]}
                    pathOptions={{
                      color: '#ef233c',
                      weight: 4,
                      opacity,
                    }}
                  />
                )
              })}

            {/* Older saved positions. Older points are fainter. */}
            {positions.slice(0, -1).map((position, index) => {
              const opacity =
                0.15 +
                0.65 *
                  ((index + 1) / Math.max(1, positions.length - 1))

              return (
                <CircleMarker
                  key={`${collarId}-history-${index}`}
                  center={position}
                  radius={5}
                  pathOptions={{
                    color: '#ef233c',
                    fillColor: '#ef233c',
                    fillOpacity: opacity,
                    opacity,
                    weight: 1,
                  }}
                >
                  <Popup>
                    <strong>Collar {collarId}</strong>
                    <br />
                    Historical position
                    <br />
                    {new Date(
                      history[index].recorded_at
                    ).toLocaleString()}
                  </Popup>
                </CircleMarker>
              )
            })}

            {/* Bright red current position. */}
            <CircleMarker
              center={currentPosition}
              radius={9}
              pathOptions={{
                color: '#9b1023',
                fillColor: '#ff1e3c',
                fillOpacity: 1,
                opacity: 1,
                weight: 3,
              }}
              eventHandlers={{
                click: () => onSelectCow(cow),
              }}
            >
              <Popup>
                <strong>Collar {collarId}</strong>
                <br />
                Current position
                <br />
                Activity: {cow.activity ?? 'Unknown'}
                <br />
                Temperature:{' '}
                {cow.cow_temperature_c ?? 'N/A'} °C
              </Popup>
            </CircleMarker>
          </div>
        )
      })}
    </MapContainer>
  )
}

export default CowMap