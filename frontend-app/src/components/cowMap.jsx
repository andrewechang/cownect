import {
  MapContainer,
  TileLayer,
  Marker,
  Popup,
} from 'react-leaflet'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'

const cowIcon = new L.Icon({
  iconUrl: '/cow-marker.png',
  iconSize: [40, 40],
  iconAnchor: [20, 40],
  popupAnchor: [0, -40],
})

function CowMap({ cows, onSelectCow }) {
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

      {cows.map((cow) => (
        <Marker
          key={cow.collar_id}
          position={[cow.latitude, cow.longitude]}
          icon={cowIcon}
          eventHandlers={{
            click: () => onSelectCow(cow),
          }}
        >
          <Popup>
            <strong>{cow.collar_id}</strong>
            <br />
            Activity: {cow.activity ?? 'Unknown'}
            <br />
            Temperature: {cow.cow_temperature_c ?? 'N/A'} °C
          </Popup>
        </Marker>
      ))}
    </MapContainer>
  )
}

export default CowMap