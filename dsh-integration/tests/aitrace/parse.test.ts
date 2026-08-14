import { describe, expect, it } from 'vitest'
import { parseWaveStat } from '../../src/aitrace/parse.ts'

describe('parseWaveStat', () => {
  it('parses the three link-quality metrics and summary', () => {
    const text = [
      'rate 999.924 f/s   crc_err 0   seq_lost 0',
      'rate 998.1 f/s   crc_err 1   seq_lost 2',
      'summary: 3032 frames (50.001 KB total), crc_err 1, seq_lost 2',
    ].join('\n')
    const r = parseWaveStat(text)
    expect(r).not.toBeNull()
    expect(r!.rate).toBeCloseTo(998.1)
    expect(r!.crc_err).toBe(1)
    expect(r!.seq_lost).toBe(2)
    expect(r!.summary).toContain('3032 frames')
  })

  it('takes the last rate line when multiple samples exist', () => {
    const r = parseWaveStat('rate 1000.1 f/s   crc_err 0   seq_lost 0\nrate 990.5 f/s   crc_err 2   seq_lost 1')
    expect(r!.rate).toBeCloseTo(990.5)
    expect(r!.crc_err).toBe(2)
    expect(r!.seq_lost).toBe(1)
  })

  it('returns null for unrecognized output', () => {
    expect(parseWaveStat('nothing here')).toBeNull()
  })
})
