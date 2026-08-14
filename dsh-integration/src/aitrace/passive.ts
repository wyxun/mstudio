import type { Context } from '@deepseek-ai/cordis'
import { defineTool } from '@deepseek-ai/dsh-tools'
import { join } from 'node:path'
import type { AitraceConfigT } from './types.ts'
import { runAitrace } from './runner.ts'
import { parseWaveStat, type WaveStatResult } from './parse.ts'

const text = (value: unknown): string => String(value)
const renderText = (_args: never, value: unknown) => [{ type: 'text' as const, text: text(value) }]

/**
 * Register the passive aitrace tools (no CPU interruption, no approval).
 * Safety model: shell/wave/serial/map/crash commands never halt the MCU.
 */
export function registerPassiveAitraceTools(ctx: Context, cfg: AitraceConfigT): void {
  ctx.tools.register(defineTool({
    name: 'aitrace_shell',
    description:
      'Send a shell command to the MCU over RTT Ch0 (passive, no CPU interruption). '
      + 'Use for firmware shell commands: regs, peek <hex_addr>, stack [n], cfsr, list, ver, log, wave drop. '
      + 'Default filters [T]-level log lines; set raw=true to disable filtering.',
    parameters: {
      command: { type: 'string', required: true, description: 'Shell command, e.g. "regs" or "peek 0x20000000"' },
      raw: { type: 'boolean', description: 'Disable [T] log filtering' },
    },
    output: {
      schema: { type: 'string' },
      render: renderText,
    },
    async execute(args, exec) {
      const argv = args.raw ? ['shell', '--raw', args.command] : ['shell', args.command]
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace shell failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_wave_stat',
    description:
      'Waveform link-quality statistics over RTT Ch1 (passive). Returns rate (f/s), crc_err, seq_lost. '
      + 'Acceptance: rate≈push rate (1000 for 1kHz), crc_err=0, seq_lost=0. Cross-check device-side drops with aitrace_shell "wave drop".',
    parameters: {
      seconds: { type: 'number', description: 'Observation window in seconds (default 5)' },
    },
    output: {
      schema: {
        type: 'object',
        properties: {
          rate: { type: 'number', required: true },
          crc_err: { type: 'number', required: true },
          seq_lost: { type: 'number', required: true },
          summary: { type: 'string' },
        },
        additionalProperties: false,
      },
      render: (_a, v: WaveStatResult) => [{
        type: 'text',
        text: `rate=${v.rate} f/s, crc_err=${v.crc_err}, seq_lost=${v.seq_lost}\n${v.summary ?? ''}`,
      }],
    },
    async execute(args, exec) {
      const seconds = args.seconds ?? 5
      const r = await runAitrace(ctx, cfg, ['wave', 'stat', String(seconds)], { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace wave stat failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      const parsed = parseWaveStat(r.stdout)
      if (!parsed) throw new Error(`unrecognized wave stat output:\n${r.stdout}`)
      return parsed
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_wave_capture',
    description:
      'Capture waveform to CSV over RTT Ch1 (passive). Saves to captureDir with a timestamped name and returns the path.',
    parameters: {
      seconds: { type: 'number', required: true, description: 'Capture duration in seconds' },
      channels: { type: 'string', description: 'Optional comma-separated channel list; empty captures all channels' },
    },
    output: {
      schema: { type: 'object', properties: { csvPath: { type: 'string' } }, additionalProperties: false },
      render: (_a, v: { csvPath: string }) => [{ type: 'text', text: `Captured to ${v.csvPath}` }],
    },
    async execute(args, exec) {
      const name = `wave_${new Date().toISOString().replace(/[:.]/g, '-')}.csv`
      const out = join(cfg.workDir, cfg.captureDir, name)
      const argv = ['wave', 'capture', String(args.seconds), '--output', out]
      if (args.channels) argv.push('--channels', args.channels)
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal, timeoutMs: (args.seconds + 20) * 1000 })
      if (r.exitCode !== 0) throw new Error(`aitrace wave capture failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return { csvPath: out }
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_wave_control',
    description:
      'Control waveform streaming over RTT Ch1 (passive): list channels, start/stop capture, set extraction rate (1=fastest, 0=external drive).',
    parameters: {
      action: { type: 'string', enum: ['list', 'start', 'stop', 'rate'], required: true, description: 'Action to perform' },
      rate: { type: 'number', description: 'Extraction rate; required when action=rate' },
    },
    output: {
      schema: { type: 'string' },
      render: renderText,
    },
    async execute(args, exec) {
      const argv = ['wave', args.action]
      if (args.action === 'rate') {
        if (args.rate === undefined) throw new Error('rate is required when action=rate')
        argv.push(String(args.rate))
      }
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace wave control failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_serial',
    description:
      'Serial port capture and interactive transmit (passive, zero intrusion). '
      + 'Read-only: listen to COMx for duration seconds. Closed loop: --send transmits a command first, then listens. '
      + 'Supports --send-hex for raw bytes like "A5 5A 01" and --hex output.',
    parameters: {
      port: { type: 'string', required: true, description: 'Serial port, e.g. "COM3"' },
      baud: { type: 'number', description: 'Baud rate (default 115200)' },
      send: { type: 'string', description: 'ASCII command to transmit before listening' },
      sendHex: { type: 'string', description: 'Hex bytes to transmit, e.g. "A5 5A 01"' },
      duration: { type: 'number', description: 'Listen duration in seconds (default 5)' },
      hexOutput: { type: 'boolean', description: 'Print received bytes as hex' },
    },
    output: {
      schema: { type: 'string' },
      render: renderText,
    },
    async execute(args, exec) {
      const argv = ['serial', '--port', args.port, '--baud', String(args.baud ?? 115200)]
      if (args.send) argv.push('--send', args.send)
      if (args.sendHex) argv.push('--send-hex', args.sendHex)
      argv.push('--duration', String(args.duration ?? 5))
      if (args.hexOutput) argv.push('--hex')
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace serial failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_map_resolve',
    description:
      'Resolve memory addresses to symbols in an ELF file (passive). Pass hex addresses like "0x08004a82".',
    parameters: {
      addresses: { type: 'array', items: { type: 'string' }, required: true, description: 'Hex addresses, e.g. ["0x08004a82", "0x08004bff"]' },
      elf: { type: 'string', description: 'ELF path; defaults to the configured project ELF' },
    },
    output: {
      schema: { type: 'string' },
      render: renderText,
    },
    async execute(args, exec) {
      const argv = ['map', 'resolve', args.elf ?? cfg.elfPath, ...args.addresses]
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace map resolve failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_map_info',
    description: 'Show memory section layout from an ELF or linker map file (passive).',
    parameters: {
      target: { type: 'string', description: 'ELF or .map path; defaults to the configured project map' },
    },
    output: {
      schema: { type: 'string' },
      render: renderText,
    },
    async execute(args, exec) {
      const argv = ['map', 'info', args.target ?? cfg.mapPath]
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace map info failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))

  ctx.tools.register(defineTool({
    name: 'aitrace_crash_report',
    description:
      'Analyze a HardFault from PC/LR/SP extracted from the RTT exception dump (passive). '
      + 'Symbolizes the faulting instruction, decodes CFSR fault flags, and interprets the return address.',
    parameters: {
      pc: { type: 'string', required: true, description: 'Faulting PC as hex, e.g. "0x08004a82"' },
      lr: { type: 'string', required: true, description: 'Link register as hex, e.g. "0x08004bff"' },
      sp: { type: 'string', required: true, description: 'Stack pointer as hex, e.g. "0x20000abc"' },
      elf: { type: 'string', description: 'ELF path; defaults to the configured project ELF' },
      cfsr: { type: 'string', description: 'CFSR register value as hex, e.g. "0x00000100"' },
    },
    output: {
      schema: { type: 'string' },
      render: renderText,
    },
    async execute(args, exec) {
      const argv = [
        'crash', 'report',
        `--pc=${args.pc}`, `--lr=${args.lr}`, `--sp=${args.sp}`,
        `--elf=${args.elf ?? cfg.elfPath}`,
      ]
      if (args.cfsr) argv.push(`--cfsr=${args.cfsr}`)
      const r = await runAitrace(ctx, cfg, argv, { signal: exec.signal })
      if (r.exitCode !== 0) throw new Error(`aitrace crash report failed (exit ${r.exitCode}): ${r.stderr || r.stdout}`)
      return r.stdout
    },
  }))
}
