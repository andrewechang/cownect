
function formatValue(value) {
  if (value === null || value === undefined) {
    return 'N/A'
  }

  if (typeof value === 'boolean') {
    return value ? 'Yes' : 'No'
  }

  if (typeof value === 'object') {
    return JSON.stringify(value)
  }

  return String(value)
}

function DataTable({ data }) {
  return (
    <table className="telemetry-table">
      <tbody>
        {Object.entries(data).map(([key, value]) => (
          <tr key={key}>
            <th>{key.replaceAll('_', ' ')}</th>
            <td>{formatValue(value)}</td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

export default function TelemetryDetails({
  reading,
  onClose,
}) {
  if (!reading) return null

  const telemetry = reading.telemetry ?? {}

  const {
    blocks = [],
    loudness_db = [],
    ...summary
  } = telemetry

  return (
    <div className="telemetry-overlay">
      <div className="telemetry-dialog">
        <div className="details-heading">
          <div>
            <h2>Telemetry — Collar {reading.collar_id}</h2>
            <small>
              {new Date(
                reading.created_at
              ).toLocaleString()}
            </small>
          </div>

          <button
            className="close-button"
            onClick={onClose}
            aria-label="Close telemetry"
          >
            ×
          </button>
        </div>

        <h3>Complete Receiver Data</h3>

        {Object.keys(summary).length > 0 ? (
          <DataTable data={summary} />
        ) : (
          <p>
            Complete telemetry is unavailable for
            this older reading.
          </p>
        )}

        {blocks.length > 0 && (
          <>
            <h3>IMU and Sound Blocks</h3>

            {blocks.map((block, index) => (
              <div
                key={index}
                className="telemetry-block"
              >
                <h4>Block {index + 1}</h4>
                <DataTable data={block} />
              </div>
            ))}
          </>
        )}

        {loudness_db.length > 0 && (
          <>
            <h3>Loudness per Second (dB)</h3>
            <DataTable
              data={Object.fromEntries(
                loudness_db.map((value, index) => [
                  `Second ${index + 1}`,
                  value,
                ])
              )}
            />
          </>
        )}

        <h3>Database Metadata</h3>
        <DataTable
          data={{
            reading_id: reading.id,
            collar_id: reading.collar_id,
            received_at: reading.created_at,
            packet_type: reading.packet_type,
            capture_id: reading.capture_id,
          }}
        />
      </div>
    </div>
  )
}
