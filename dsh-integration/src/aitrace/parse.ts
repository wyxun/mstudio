export interface WaveStatResult {
  rate: number
  crc_err: number
  seq_lost: number
  summary?: string
}

/**
 * Parse `wave stat` stdout into link-quality metrics. The last complete
 * per-second sample line wins; the summary line is kept verbatim when present.
 * Returns null when no complete sample line is recognized.
 */
export function parseWaveStat(text: string): WaveStatResult | null {
  let result: WaveStatResult | null = null
  for (const line of text.split(/\r?\n/)) {
    const rate = /rate\s+([\d.]+)\s+f\/s/.exec(line)
    const crcErr = /crc_err\s+(\d+)/.exec(line)
    const seqLost = /seq_lost\s+(\d+)/.exec(line)
    if (!rate || !crcErr || !seqLost) continue
    result = { rate: Number(rate[1]), crc_err: Number(crcErr[1]), seq_lost: Number(seqLost[1]) }
  }
  if (!result) return null
  const summary = /summary:\s+(.+)/.exec(text)
  if (summary?.[1]) result.summary = summary[1]
  return result
}
