param(
    [string]$Port = 'COM7',

    [ValidateRange(1, 100000)]
    [int]$Samples = 10,

    [switch]$RequireHealthy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$pattern = (
    '^HUB seq=(?<seq>\d+)' +
    ' T=(?<temperature>-?\d+\.\d{2})C' +
    ' temp_valid=(?<tv>[01])' +
    ' X=(?<x>-?\d+)' +
    ' Y=(?<y>-?\d+)' +
    ' Z=(?<z>-?\d+) mg' +
    ' accel_valid=(?<av>[01])' +
    ' temp_err=(?<te>\d+)' +
    ' accel_err=(?<ae>\d+)' +
    ' tx_timeout=(?<tx>\d+)$'
)

$connection = [System.IO.Ports.SerialPort]::new(
    $Port,
    115200,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)

$connection.Handshake = [System.IO.Ports.Handshake]::None
$connection.ReadTimeout = 5000
$connection.NewLine = "`n"
$connection.DtrEnable = $false
$connection.RtsEnable = $false

$previousSequence = $null

try {
    $connection.Open()
    $connection.DiscardInBuffer()

    # Connection may begin partway through a report.
    $null = $connection.ReadLine()

    for ($index = 0; $index -lt $Samples; $index++) {
        $reportMatch = $null

        # Allow limited resynchronization after malformed lines.
        for ($attempt = 0; $attempt -lt 5; $attempt++) {
            $line = $connection.ReadLine().Trim()
            $candidate = [regex]::Match($line, $pattern)

            if ($candidate.Success) {
                $reportMatch = $candidate
                break
            }

            Write-Warning "Skipped malformed report: $line"
        }

        if ($null -eq $reportMatch) {
            throw 'Could not find a complete HUB report.'
        }

        $groups = $reportMatch.Groups

        $record = [PSCustomObject]@{
            Sequence = [uint32]::Parse($groups['seq'].Value)

            Temperature_C = [decimal]::Parse(
                $groups['temperature'].Value,
                [System.Globalization.CultureInfo]::InvariantCulture
            )

            TempValid  = ($groups['tv'].Value -eq '1')
            X_mg       = [int32]::Parse($groups['x'].Value)
            Y_mg       = [int32]::Parse($groups['y'].Value)
            Z_mg       = [int32]::Parse($groups['z'].Value)
            AccelValid = ($groups['av'].Value -eq '1')
            TempErrors = [uint32]::Parse($groups['te'].Value)
            AccelErrors = [uint32]::Parse($groups['ae'].Value)
            TxTimeouts = [uint32]::Parse($groups['tx'].Value)
        }

        if ($null -ne $previousSequence) {
            $expectedSequence = [uint32](
                ([uint64]$previousSequence + 1) %
                [uint64]4294967296
            )

            if ($record.Sequence -ne $expectedSequence) {
                $message = (
                    "Sequence discontinuity: expected " +
                    "$expectedSequence, received $($record.Sequence)."
                )

                if ($RequireHealthy) {
                    throw $message
                }

                Write-Warning $message
            }
        }

        $previousSequence = $record.Sequence

        if ($RequireHealthy) {
            if ((-not $record.TempValid) -or
                (-not $record.AccelValid) -or
                ($record.TempErrors -ne 0) -or
                ($record.AccelErrors -ne 0) -or
                ($record.TxTimeouts -ne 0)) {
                throw "Unhealthy report: $line"
            }
        }

        Write-Output $record
    }
}
finally {
    if ($connection.IsOpen) {
        $connection.Close()
    }

    $connection.Dispose()
}