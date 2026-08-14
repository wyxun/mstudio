/** Configuration for the aitrace tool family. Pure type; schema lives in config.ts. */
export interface AitraceConfigT {
  /** Repository root of the firmware project; every relative path resolves here. */
  workDir: string
  /** Path to aitrace.exe, absolute or relative to workDir. */
  aitracePath: string
  /** ELF used by map/crash/gdb when the model does not pass one. */
  elfPath: string
  /** Linker map used by map info. */
  mapPath: string
  /** Directory (relative to workDir) for wave captures. */
  captureDir: string
  /** Master switch for halt/GDB per-call approval; false bypasses the gate. */
  approval: boolean
  /** Fallback per-invocation timeout in milliseconds. */
  shellTimeoutMs: number
}
