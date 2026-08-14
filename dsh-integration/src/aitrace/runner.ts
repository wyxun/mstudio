import type { Context } from '@deepseek-ai/cordis'
import type { SubprocessOutcome } from '@deepseek-ai/dsh-subprocess'
import { resolve } from 'node:path'
import type { AitraceConfigT } from './types.ts'

/** Resolve the aitrace executable to an absolute path. */
export function resolveAitraceExe(cfg: AitraceConfigT): string {
  return resolve(cfg.workDir, cfg.aitracePath)
}

/** Build the full argv for one aitrace invocation. */
export function buildAitraceArgv(cfg: AitraceConfigT, args: string[]): string[] {
  return [resolveAitraceExe(cfg), ...args]
}

export interface AitraceRunResult {
  stdout: string
  stderr: string
  exitCode: number | null
  signal: NodeJS.Signals | null
}

/**
 * Spawn one aitrace invocation through ctx.subprocess with bounded collected
 * output. Aborts (timeout or caller signal) terminate the process tree and
 * reject with the abort reason; otherwise returns exit facts plus collected
 * streams. Callers decide whether a non-zero exit is an error (throw) or a
 * domain outcome (return) per tool.
 */
/**
 * Spawn one CLI invocation through ctx.subprocess with bounded collected
 * output. Aborts (timeout or caller signal) terminate the process tree and
 * reject with the abort reason; otherwise returns exit facts plus collected
 * streams. Callers decide whether a non-zero exit is an error (throw) or a
 * domain outcome (return) per tool.
 */
export async function runTool(
  ctx: Context,
  opts: { exe: string; cwd: string; timeoutMs: number },
  args: string[],
  extra: { timeoutMs?: number; signal?: AbortSignal } = {},
): Promise<AitraceRunResult> {
  const timeoutMs = extra.timeoutMs ?? opts.timeoutMs
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  const outer = extra.signal
  if (outer) {
    if (outer.aborted) controller.abort()
    else outer.addEventListener('abort', () => controller.abort(), { once: true })
  }
  try {
    const handle = ctx.subprocess.spawn({
      argv: [opts.exe, ...args],
      cwd: opts.cwd,
      stdio: {
        stdin: 'ignore',
        stdout: { maxBytes: 4 * 1024 * 1024, spill: { maxBytes: 8 * 1024 * 1024 } },
        stderr: { maxBytes: 1024 * 1024 },
      },
      graceMs: 2000,
      signal: controller.signal,
    })
    const outcome: SubprocessOutcome = await handle.done
    const stdout = await handle.collected.stdout?.readFrom(0)
    const stderr = await handle.collected.stderr?.readFrom(0)
    return {
      stdout: stdout?.text ?? '',
      stderr: stderr?.text ?? '',
      exitCode: outcome.exitCode,
      signal: outcome.signal,
    }
  } finally {
    clearTimeout(timer)
  }
}

/** aitrace-specific wrapper over runTool with the plugin's aitrace config. */
export async function runAitrace(
  ctx: Context,
  cfg: AitraceConfigT,
  args: string[],
  opts: { timeoutMs?: number; signal?: AbortSignal } = {},
): Promise<AitraceRunResult> {
  return runTool(
    ctx,
    { exe: resolveAitraceExe(cfg), cwd: cfg.workDir, timeoutMs: cfg.shellTimeoutMs },
    args,
    opts,
  )
}
