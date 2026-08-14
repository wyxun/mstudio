import { describe, expect, it } from 'vitest'
import { gateByApproval, buildIntrusiveArgv } from '../../src/aitrace/gate.ts'
import type { AitraceConfigT } from '../../src/aitrace/types.ts'

const cfg: AitraceConfigT = {
  workDir: 'D:/proj',
  aitracePath: 'tools/aitrace.exe',
  elfPath: 'build/template.elf',
  mapPath: 'build/template.map',
  captureDir: 'captures',
  approval: true,
  shellTimeoutMs: 15000,
}

const agent = {} as never

describe('gateByApproval', () => {
  it('allows when approval returns allowed-once', async () => {
    const approval = { request: async () => 'allowed-once' } as never
    expect(await gateByApproval(cfg, approval, agent, 'aitrace_ocd_regs', 'reason')).toBe(true)
  })

  it('blocks when approval returns rejected', async () => {
    const approval = { request: async () => 'rejected' } as never
    expect(await gateByApproval(cfg, approval, agent, 'aitrace_ocd_halt', 'reason')).toBe(false)
  })

  it('fails closed without an approval service', async () => {
    expect(await gateByApproval(cfg, undefined, agent, 'aitrace_gdb_connect', 'reason')).toBe(false)
  })

  it('passes through when approval config is disabled', async () => {
    expect(await gateByApproval({ ...cfg, approval: false }, undefined, agent, 'x', 'y')).toBe(true)
  })
})

describe('buildIntrusiveArgv', () => {
  it('maps ocd peek args to CLI tokens', () => {
    const argv = buildIntrusiveArgv('aitrace_ocd_peek', cfg, { address: '0x20000000' } as never)
    expect(argv).toEqual(['ocd', 'peek', '0x20000000'])
  })

  it('defaults gdb connect elf to the configured elfPath', () => {
    const argv = buildIntrusiveArgv('aitrace_gdb_connect', cfg, {} as never)
    expect(argv).toEqual(['gdb', 'connect', '--elf', 'build/template.elf'])
  })

  it('maps gdb break location', () => {
    const argv = buildIntrusiveArgv('aitrace_gdb_break', cfg, { location: 'main.c:100' } as never)
    expect(argv).toEqual(['gdb', 'break', 'main.c:100'])
  })
})
