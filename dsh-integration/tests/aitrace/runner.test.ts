import { describe, expect, it } from 'vitest'
import { buildAitraceArgv, resolveAitraceExe } from '../../src/aitrace/runner.ts'

// Windows path normalization helper: node:path resolves with backslashes on
// win32; compare in forward-slash form for stable expectations.
const norm = (p: string) => p.replace(/\\/g, '/')

const cfg = {
  workDir: 'D:/proj',
  aitracePath: 'tools/aitrace.exe',
  elfPath: 'build/template.elf',
  mapPath: 'build/template.map',
  captureDir: 'captures',
  approval: true,
  shellTimeoutMs: 15000,
}

describe('buildAitraceArgv', () => {
  it('builds argv with workdir-relative exe and passes command tokens through', () => {
    const argv = buildAitraceArgv(cfg, ['shell', 'regs'])
    expect(norm(argv[0])).toBe('D:/proj/tools/aitrace.exe')
    expect(argv.slice(1)).toEqual(['shell', 'regs'])
  })

  it('builds capture argv with output file last', () => {
    const argv = buildAitraceArgv(cfg, ['wave', 'capture', '5', '--output', 'cap.csv'])
    expect(norm(argv[0])).toBe('D:/proj/tools/aitrace.exe')
    expect(argv[argv.length - 1]).toBe('cap.csv')
    expect(argv.slice(1, 4)).toEqual(['wave', 'capture', '5'])
  })
})

describe('resolveAitraceExe', () => {
  it('joins workDir with relative aitracePath', () => {
    expect(norm(resolveAitraceExe(cfg))).toBe('D:/proj/tools/aitrace.exe')
  })

  it('keeps absolute aitracePath as-is', () => {
    expect(norm(resolveAitraceExe({ ...cfg, aitracePath: 'C:/tools/aitrace.exe' }))).toBe('C:/tools/aitrace.exe')
  })
})
