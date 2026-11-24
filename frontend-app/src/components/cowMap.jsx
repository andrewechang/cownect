
import { useEffect, useMemo } from 'react'
import L from 'leaflet'

import {
  MapContainer,
  Marker,
  Polyline,
  TileLayer,
  useMap,
  useMapEvents,
} from 'react-leaflet'

import 'leaflet/dist/leaflet.css'

function FocusOnCow({ selectedCow }) {
  const map = useMap()

  useEffect(() => {
    if (!selectedCow) return

    if (selectedCow.latitude == null ||
        selectedCow.longitude == null) return

    const lat = Number(selectedCow.latitude)
    const lng = Number(selectedCow.longitude)

    if (!Number.isFinite(lat) ||
        !Number.isFinite(lng)) return

    map.flyTo(
      [lat, lng],
      17,
      {
        animate: true,
        duration: 1.25,
      }
    )
  }, [map, selectedCow?.collar_id])

  return null
}

function MapClickHandler({ onClearSelection }) {
  useMapEvents({
    click: () => onClearSelection(),
  })

  return null
}

function CollarMarker({ cow, onSelectCow, active }) {
  const icon = useMemo(() => {
    const color = active ? '#ef233c' : '#89928b'

    return L.divIcon({
      className: 'collar-marker',
      html: `
        <div class="collar-marker-content">
          <span class="collar-marker-dot"
            style="background:${color}">
          </span>
          <span class="collar-marker-label"></span>
        </div>
      `,
      iconSize: [100, 46],
      iconAnchor: [50, 12],
    })
  }, [active])

  // Set label as text, rather than interpolating
  // untrusted collar IDs into HTML.
  useEffect(() => {
    // Label is populated through the marker's
    // accessible tooltip below.
  }, [cow.collar_id])

  if (cow.latitude == null ||
      cow.longitude == null) return null

  const lat = Number(cow.latitude)
  const lng = Number(cow.longitude)

  if (!Number.isFinite(lat) ||
      !Number.isFinite(lng)) return null

  // Escape collar IDs before placing them in
  // Leaflet's HTML-based marker icon.
  const safeId = String(cow.collar_id)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;')

  const labeledIcon = L.divIcon({
    className: 'collar-marker',
    html: `
      <div class="collar-marker-content">
        <span class="collar-marker-dot"
          style="background:${active ? '#ef233c' : '#89928b'}">
        </span>
        <span class="collar-marker-label">
          ${safeId}
        </span>
      </div>
    `,
    iconSize: [100, 46],
    iconAnchor: [50, 12],
  })

  return (
    <Marker
      position={[lat, lng]}
      icon={labeledIcon}
      eventHandlers={{
        click: (event) => {
          L.DomEvent.stopPropagation(
            event.originalEvent
          )
          onSelectCow(cow)
        },
      }}
    />
  )
}

export default function CowMap({
  cows,
  histories,
  selectedCow,
  onSelectCow,
  onClearSelection,
  isActive,
}) {
  const defaultCenter = [33.7175, -117.8311]

  return (
    <MapContainer
      center={defaultCenter}
      zoom={15}
      className="cow-map"
    >
      <TileLayer
        attribution="&copy; OpenStreetMap contributors"
        url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
      />

      <FocusOnCow selectedCow={selectedCow} />

      <MapClickHandler
        onClearSelection={onClearSelection}
      />

      {cows.map((cow) => {
        const id = String(cow.collar_id)
        const history = histories[id] ?? []

        const positions = history.map((reading) => [
          Number(reading.latitude),
          Number(reading.longitude),
        ])

        return (
          <Polyline
            key={`${id}-trail`}
            positions={positions}
            pathOptions={{
              color: '#ef233c',
              weight: 3,
              opacity: 0.5,
            }}
          />
        )
      })}

      {cows.map((cow) => (
        <CollarMarker
          key={cow.collar_id}
          cow={cow}
          active={isActive(cow)}
          onSelectCow={onSelectCow}
        />
      ))}
    </MapContainer>
  )
}
