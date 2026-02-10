'use strict';

const fs = (typeof require !== 'undefined') ? require('fs') : null;

// ---------------------------------------------------------------------------
// Memory  -- ArrayBuffer-backed virtual memory with stack & heap
// ---------------------------------------------------------------------------
class Memory {
  constructor(size) {
    this.size = size;
    this.buffer = new ArrayBuffer(size);
    this.view = new DataView(this.buffer);
    this.u8 = new Uint8Array(this.buffer);

    // Stack starts at top, grows downward.  Reserve top 1 MB for stack.
    this.stackTop = size;
    this.sp = this.stackTop;

    // Heap starts at byte 4 (address 0 is reserved NULL).
    this.heapStart = 4;

    // Free-list: array of {addr, size} ordered by addr.  Initially one big
    // block covering the whole heap region (up to stackBottom).
    this.stackReserve = 1024 * 1024; // 1 MB
    this.heapEnd = size - this.stackReserve;
    this.freeList = [{ addr: this.heapStart, size: this.heapEnd - this.heapStart }];
    // Allocated blocks: addr -> usable size (for realloc / free bookkeeping)
    this.allocated = new Map();
  }

  // Reserve memory for global variables so heap doesn't overlap
  reserveGlobals(endAddr) {
    this.heapStart = endAddr;
    this.freeList = [{ addr: endAddr, size: this.heapEnd - endAddr }];
  }

  // -- typed read helpers (little-endian) --
  readInt8(addr)    { return this.view.getInt8(addr); }
  readUint8(addr)   { return this.view.getUint8(addr); }
  readInt16(addr)   { return this.view.getInt16(addr, true); }
  readUint16(addr)  { return this.view.getUint16(addr, true); }
  readInt32(addr)   { return this.view.getInt32(addr, true); }
  readUint32(addr)  { return this.view.getUint32(addr, true); }
  readFloat32(addr) { return this.view.getFloat32(addr, true); }
  readFloat64(addr) { return this.view.getFloat64(addr, true); }

  // -- typed write helpers (little-endian) --
  writeInt8(addr, v)    { this.view.setInt8(addr, v); }
  writeUint8(addr, v)   { this.view.setUint8(addr, v); }
  writeInt16(addr, v)   { this.view.setInt16(addr, v, true); }
  writeUint16(addr, v)  { this.view.setUint16(addr, v, true); }
  writeInt32(addr, v)   { this.view.setInt32(addr, v, true); }
  writeUint32(addr, v)  { this.view.setUint32(addr, v, true); }
  writeFloat32(addr, v) { this.view.setFloat32(addr, v, true); }
  writeFloat64(addr, v) { this.view.setFloat64(addr, v, true); }

  // -- 64-bit integer helpers (BigInt) --
  readBigUint64(addr) { return this.view.getBigUint64(addr, true); }
  readBigInt64(addr)  { return this.view.getBigInt64(addr, true); }
  writeBigUint64(addr, val) { this.view.setBigUint64(addr, BigInt(val), true); }
  writeBigInt64(addr, val)  { this.view.setBigInt64(addr, BigInt(val), true); }

  // -- string helpers --
  readString(addr) {
    let s = '';
    for (let i = addr; i < this.size; i++) {
      const ch = this.u8[i];
      if (ch === 0) break;
      s += String.fromCharCode(ch);
    }
    return s;
  }

  writeString(addr, str) {
    for (let i = 0; i < str.length; i++) {
      this.u8[addr + i] = str.charCodeAt(i) & 0xFF;
    }
    this.u8[addr + str.length] = 0;
  }

  allocString(str) {
    const ptr = this.malloc(str.length + 1);
    if (ptr) this.writeString(ptr, str);
    return ptr;
  }

  // -- stack operations --
  stackAlloc(bytes) {
    bytes = align(bytes, 8);
    this.sp -= bytes;
    return this.sp;
  }

  stackFree(bytes) {
    bytes = align(bytes, 8);
    this.sp += bytes;
  }

  // -- heap allocator (first-fit with coalescing free list) --
  malloc(reqSize) {
    if (reqSize <= 0) return 0;
    const size = align(reqSize, 8);
    for (let i = 0; i < this.freeList.length; i++) {
      const block = this.freeList[i];
      if (block.size >= size) {
        const addr = block.addr;
        if (block.size - size >= 16) {
          // Split
          block.addr += size;
          block.size -= size;
        } else {
          this.freeList.splice(i, 1);
        }
        this.allocated.set(addr, size);
        return addr;
      }
    }
    return 0; // out of memory
  }

  calloc(count, elemSize) {
    const total = count * elemSize;
    const ptr = this.malloc(total);
    if (ptr) {
      this.u8.fill(0, ptr, ptr + align(total, 8));
    }
    return ptr;
  }

  realloc(ptr, newSize) {
    if (ptr === 0) return this.malloc(newSize);
    if (newSize === 0) { this.free(ptr); return 0; }
    const oldSize = this.allocated.get(ptr);
    if (oldSize === undefined) return 0;
    if (align(newSize, 8) <= oldSize) {
      return ptr; // fits already
    }
    const newPtr = this.malloc(newSize);
    if (newPtr === 0) return 0;
    const copyLen = Math.min(oldSize, newSize);
    this.u8.copyWithin(newPtr, ptr, ptr + copyLen);
    this.free(ptr);
    return newPtr;
  }

  free(ptr) {
    if (ptr === 0) return;
    const size = this.allocated.get(ptr);
    if (size === undefined) return;
    this.allocated.delete(ptr);
    // Insert into free list sorted by address, then coalesce neighbours.
    const entry = { addr: ptr, size };
    let idx = 0;
    while (idx < this.freeList.length && this.freeList[idx].addr < ptr) idx++;
    this.freeList.splice(idx, 0, entry);
    // Coalesce with next
    if (idx + 1 < this.freeList.length) {
      const next = this.freeList[idx + 1];
      if (entry.addr + entry.size === next.addr) {
        entry.size += next.size;
        this.freeList.splice(idx + 1, 1);
      }
    }
    // Coalesce with prev
    if (idx > 0) {
      const prev = this.freeList[idx - 1];
      if (prev.addr + prev.size === entry.addr) {
        prev.size += entry.size;
        this.freeList.splice(idx, 1);
      }
    }
  }
}

function align(v, a) {
  return (v + a - 1) & ~(a - 1);
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
class Runtime {
  constructor(memorySize) {
    this.mem = new Memory(memorySize || 16 * 1024 * 1024);
    this._stdout = '';
    this._stderr = '';
    this._exitCode = null;
    this._randState = 1;
    this._startTime = Date.now();

    // Scratch buffer for float64 <-> BigInt conversions (NaN-safe)
    this._tmpBuf = new ArrayBuffer(8);
    this._tmpView = new DataView(this._tmpBuf);

    // setjmp/longjmp state
    this._setjmpCounter = 0;

    // Function pointer table: integer ID → JS function
    this._funcTable = [null]; // index 0 = NULL function pointer
    this._funcMap = new Map(); // fn → id (for deduplication)

    // File descriptor table.  0=stdin 1=stdout 2=stderr, fd>=3 user files
    this._nextFd = 3;
    this._files = new Map();
    // Pseudo-file handles stored as pointers (we use small sentinel addrs)
    this.STDIN_PTR  = 1;
    this.STDOUT_PTR = 2;
    this.STDERR_PTR = 3;
    // Map pointer -> fd
    this._filePtrToFd = new Map([
      [this.STDIN_PTR, 0],
      [this.STDOUT_PTR, 1],
      [this.STDERR_PTR, 2],
    ]);
    this._fdToFilePtr = new Map([
      [0, this.STDIN_PTR],
      [1, this.STDOUT_PTR],
      [2, this.STDERR_PTR],
    ]);

    // Stdin buffer for scanf
    this._stdinBuf = '';
    this._stdinPos = 0;
    this._stdinEof = false;

    // errno support: fixed 4-byte allocation on heap for errno value
    this._errnoAddr = this.mem.malloc(4);
    this.mem.writeInt32(this._errnoAddr, 0);

    // va_list table: index → { args: [...], pos: 0 }
    this._vaLists = [null]; // index 0 reserved
  }

  // ======================== float64 <-> BigInt (NaN-safe) ===================
  // Doubles are stored as BigInt (raw 64-bit IEEE 754 bits) to preserve NaN
  // payloads for NaN-boxing.  These helpers convert between representations.
  f64(bits) {
    this._tmpView.setBigUint64(0, BigInt(bits), true);
    return this._tmpView.getFloat64(0, true);
  }

  f64bits(num) {
    this._tmpView.setFloat64(0, Number(num), true);
    return this._tmpView.getBigUint64(0, true);
  }

  // ======================== function pointer table ==========================
  registerFunction(fn) {
    const existing = this._funcMap.get(fn);
    if (existing !== undefined) return existing;
    const id = this._funcTable.length;
    this._funcTable.push(fn);
    this._funcMap.set(fn, id);
    return id;
  }

  callFunction(id, ...args) {
    const fn = this._funcTable[id];
    if (!fn) throw new Error('NULL function pointer call (id=' + id + ')');
    return fn(...args);
  }

  // ======================== va_list support =================================
  vaStart(jsArgs) {
    const id = this._vaLists.length;
    this._vaLists.push({ args: [...jsArgs], pos: 0 });
    return id;
  }

  vaEnd(id) {
    if (id > 0 && id < this._vaLists.length) this._vaLists[id] = null;
  }

  vaCopy(srcId) {
    const src = this._vaLists[srcId];
    const id = this._vaLists.length;
    this._vaLists.push({ args: [...src.args], pos: src.pos });
    return id;
  }

  vaArg(id) {
    const entry = this._vaLists[id];
    return entry.args[entry.pos++];
  }

  // ======================== v-printf functions =============================
  vsnprintf(bufAddr, maxLen, fmtAddr, vaId) {
    const fmt = this.mem.readString(fmtAddr);
    const args = this._vaLists[vaId].args.slice(this._vaLists[vaId].pos);
    const s = this._formatString(fmt, args);
    if (bufAddr !== 0 && maxLen > 0) {
      const truncated = s.substring(0, maxLen - 1);
      this.mem.writeString(bufAddr, truncated);
    }
    return s.length;
  }

  vfprintf(filePtr, fmtAddr, vaId) {
    const fmt = this.mem.readString(fmtAddr);
    const args = this._vaLists[vaId].args.slice(this._vaLists[vaId].pos);
    const s = this._formatString(fmt, args);
    if (filePtr === this.STDOUT_PTR) {
      this._writeStdout(s);
    } else if (filePtr === this.STDERR_PTR) {
      this._writeStderr(s);
    } else {
      this._fileWrite(filePtr, s);
    }
    return s.length;
  }

  // ======================== memory forwarding ==============================
  malloc(size)           { return this.mem.malloc(size); }
  calloc(count, size)    { return this.mem.calloc(count, size); }
  realloc(ptr, size)     { return this.mem.realloc(ptr, size); }
  free(ptr)              { return this.mem.free(ptr); }

  // ======================== printf family ===================================
  _formatPrintf(fmtAddr, args) {
    const fmt = this.mem.readString(fmtAddr);
    return this._formatString(fmt, args);
  }

  _formatString(fmt, args) {
    let out = '';
    let ai = 0; // argument index
    let i = 0;
    while (i < fmt.length) {
      if (fmt[i] !== '%') { out += fmt[i++]; continue; }
      i++; // skip '%'
      if (i >= fmt.length) break;
      if (fmt[i] === '%') { out += '%'; i++; continue; }

      // Parse flags
      let flags = { minus: false, plus: false, space: false, zero: false, hash: false };
      let parsingFlags = true;
      while (parsingFlags && i < fmt.length) {
        switch (fmt[i]) {
          case '-': flags.minus = true; i++; break;
          case '+': flags.plus = true; i++; break;
          case ' ': flags.space = true; i++; break;
          case '0': flags.zero = true; i++; break;
          case '#': flags.hash = true; i++; break;
          default: parsingFlags = false;
        }
      }

      // Width
      let width = 0;
      if (fmt[i] === '*') { width = args[ai++]; i++; }
      else { while (i < fmt.length && fmt[i] >= '0' && fmt[i] <= '9') { width = width * 10 + (fmt[i] - '0'); i++; } }

      // Precision
      let prec = -1;
      if (fmt[i] === '.') {
        i++;
        prec = 0;
        if (fmt[i] === '*') { prec = args[ai++]; i++; }
        else { while (i < fmt.length && fmt[i] >= '0' && fmt[i] <= '9') { prec = prec * 10 + (fmt[i] - '0'); i++; } }
      }

      // Length modifier
      let length = '';
      if (fmt[i] === 'h') { length = 'h'; i++; if (fmt[i] === 'h') { length = 'hh'; i++; } }
      else if (fmt[i] === 'l') { length = 'l'; i++; if (fmt[i] === 'l') { length = 'll'; i++; } }
      else if (fmt[i] === 'z') { length = 'z'; i++; }

      // Conversion specifier
      const spec = fmt[i++];
      let val = args[ai++];
      let s = '';

      switch (spec) {
        case 'd': case 'i': {
          let n = this._toInt(val, length);
          let neg = n < 0;
          let abs = neg ? (-n).toString(10) : n.toString(10);
          let prefix = neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''));
          if (prec >= 0) { while (abs.length < prec) abs = '0' + abs; flags.zero = false; }
          s = prefix + abs;
          s = this._pad(s, width, flags);
          break;
        }
        case 'u': {
          let n = this._toUint(val, length);
          let abs = n.toString(10);
          if (prec >= 0) { while (abs.length < prec) abs = '0' + abs; flags.zero = false; }
          s = abs;
          s = this._pad(s, width, flags);
          break;
        }
        case 'o': {
          let n = this._toUint(val, length);
          let abs = n.toString(8);
          if (prec >= 0) { while (abs.length < prec) abs = '0' + abs; flags.zero = false; }
          if (flags.hash && abs[0] !== '0') abs = '0' + abs;
          s = this._pad(abs, width, flags);
          break;
        }
        case 'x': case 'X': {
          let n = this._toUint(val, length);
          let abs = n.toString(16);
          if (spec === 'X') abs = abs.toUpperCase();
          if (prec >= 0) { while (abs.length < prec) abs = '0' + abs; flags.zero = false; }
          let prefix = (flags.hash && n !== 0) ? (spec === 'X' ? '0X' : '0x') : '';
          s = prefix + abs;
          s = this._pad(s, width, flags);
          break;
        }
        case 'f': case 'F': {
          let n = typeof val === 'bigint' ? this.f64(val) : Number(val);
          if (prec < 0) prec = 6;
          s = this._fmtFloat(n, prec, flags, false);
          s = this._pad(s, width, flags);
          break;
        }
        case 'e': case 'E': {
          let n = typeof val === 'bigint' ? this.f64(val) : Number(val);
          if (prec < 0) prec = 6;
          s = this._fmtExp(n, prec, flags, spec === 'E');
          s = this._pad(s, width, flags);
          break;
        }
        case 'g': case 'G': {
          let n = typeof val === 'bigint' ? this.f64(val) : Number(val);
          if (prec < 0) prec = 6;
          if (prec === 0) prec = 1;
          s = this._fmtG(n, prec, flags, spec === 'G');
          s = this._pad(s, width, flags);
          break;
        }
        case 'c': {
          s = String.fromCharCode(val & 0xFF);
          s = this._pad(s, width, flags);
          break;
        }
        case 's': {
          let raw = (typeof val === 'string') ? val : this.mem.readString(val);
          if (prec >= 0) raw = raw.substring(0, prec);
          s = this._pad(raw, width, flags);
          break;
        }
        case 'p': {
          s = '0x' + ((val >>> 0).toString(16));
          s = this._pad(s, width, flags);
          break;
        }
        case 'n': {
          // %n stores current output length at the pointer
          ai--; // we consumed val as a ptr
          this.mem.writeInt32(val, out.length);
          continue;
        }
        default:
          s = '%' + spec;
          ai--; // no arg consumed for unknown
      }
      out += s;
    }
    return out;
  }

  _toInt(val, len) {
    let n = Number(val) | 0;
    if (len === 'hh') n = (n << 24) >> 24;
    else if (len === 'h') n = (n << 16) >> 16;
    return n;
  }

  _toUint(val, len) {
    let n = Number(val);
    if (len === 'hh') n = n & 0xFF;
    else if (len === 'h') n = n & 0xFFFF;
    else n = n >>> 0;
    return n;
  }

  _fmtFloat(n, prec, flags, _unused) {
    let neg = false;
    if (n < 0 || Object.is(n, -0)) { neg = true; n = -n; }
    if (!isFinite(n)) return (neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''))) + (isNaN(n) ? 'nan' : 'inf');
    let s = n.toFixed(prec);
    if (!flags.hash && prec === 0 && s.indexOf('.') === -1) { /* no dot needed */ }
    else if (flags.hash && s.indexOf('.') === -1) { s += '.'; }
    let prefix = neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''));
    return prefix + s;
  }

  _fmtExp(n, prec, flags, upper) {
    let neg = false;
    if (n < 0 || Object.is(n, -0)) { neg = true; n = -n; }
    if (!isFinite(n)) return (neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''))) + (isNaN(n) ? (upper ? 'NAN' : 'nan') : (upper ? 'INF' : 'inf'));
    let s = n.toExponential(prec);
    // Normalize exponent to at least 2 digits with +/- sign
    s = s.replace(/e([+-])(\d)$/, 'e$1' + '0$2');
    if (upper) s = s.toUpperCase();
    if (flags.hash && s.indexOf('.') === -1) {
      const ei = s.indexOf(upper ? 'E' : 'e');
      s = s.substring(0, ei) + '.' + s.substring(ei);
    }
    let prefix = neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''));
    return prefix + s;
  }

  _fmtG(n, prec, flags, upper) {
    let neg = false;
    if (n < 0 || Object.is(n, -0)) { neg = true; n = -n; }
    if (!isFinite(n)) return (neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''))) + (isNaN(n) ? (upper ? 'NAN' : 'nan') : (upper ? 'INF' : 'inf'));

    // Determine exponent
    let exp = (n === 0) ? 0 : Math.floor(Math.log10(n));
    let useExp = (exp < -4 || exp >= prec);
    let s;
    if (useExp) {
      s = n.toExponential(prec - 1);
      s = s.replace(/e([+-])(\d)$/, 'e$1' + '0$2');
      if (upper) s = s.toUpperCase();
    } else {
      let fracDigits = prec - exp - 1;
      if (fracDigits < 0) fracDigits = 0;
      s = n.toFixed(fracDigits);
    }
    // Remove trailing zeros unless # flag
    if (!flags.hash) {
      if (s.indexOf('.') !== -1) {
        const eIdx = s.search(/[eE]/);
        let mantissa, expPart;
        if (eIdx >= 0) { mantissa = s.substring(0, eIdx); expPart = s.substring(eIdx); }
        else { mantissa = s; expPart = ''; }
        mantissa = mantissa.replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '');
        s = mantissa + expPart;
      }
    } else if (s.indexOf('.') === -1) {
      const eIdx = s.search(/[eE]/);
      if (eIdx >= 0) s = s.substring(0, eIdx) + '.' + s.substring(eIdx);
      else s += '.';
    }
    let prefix = neg ? '-' : (flags.plus ? '+' : (flags.space ? ' ' : ''));
    return prefix + s;
  }

  _pad(s, width, flags) {
    if (s.length >= width) return s;
    const diff = width - s.length;
    if (flags.minus) {
      return s + ' '.repeat(diff);
    }
    if (flags.zero) {
      // Insert zeros after sign/prefix
      let signLen = 0;
      if (s[0] === '-' || s[0] === '+' || s[0] === ' ') signLen = 1;
      else if (s.substring(0, 2) === '0x' || s.substring(0, 2) === '0X') signLen = 2;
      return s.substring(0, signLen) + '0'.repeat(diff) + s.substring(signLen);
    }
    return ' '.repeat(diff) + s;
  }

  printf(fmtAddr, ...args) {
    const s = this._formatPrintf(fmtAddr, args);
    this._writeStdout(s);
    return s.length;
  }

  sprintf(bufAddr, fmtAddr, ...args) {
    const s = this._formatPrintf(fmtAddr, args);
    this.mem.writeString(bufAddr, s);
    return s.length;
  }

  snprintf(bufAddr, maxLen, fmtAddr, ...args) {
    const s = this._formatPrintf(fmtAddr, args);
    if (maxLen > 0) {
      const truncated = s.substring(0, maxLen - 1);
      this.mem.writeString(bufAddr, truncated);
    }
    return s.length;
  }

  fprintf(filePtr, fmtAddr, ...args) {
    const s = this._formatPrintf(fmtAddr, args);
    if (filePtr === this.STDOUT_PTR) {
      this._writeStdout(s);
    } else if (filePtr === this.STDERR_PTR) {
      this._writeStderr(s);
    } else {
      this._fileWrite(filePtr, s);
    }
    return s.length;
  }

  // ======================== scanf family ====================================
  scanf(fmtAddr, ...argPtrs) {
    const input = this._readStdinLine();
    const fmt = this.mem.readString(fmtAddr);
    return this._scanString(fmt, input, argPtrs);
  }

  sscanf(srcAddr, fmtAddr, ...argPtrs) {
    const src = this.mem.readString(srcAddr);
    const fmt = this.mem.readString(fmtAddr);
    return this._scanString(fmt, src, argPtrs);
  }

  _scanString(fmt, input, argPtrs) {
    let fi = 0, si = 0, matched = 0;
    let ai = 0;
    while (fi < fmt.length) {
      if (fmt[fi] !== '%') {
        if (/\s/.test(fmt[fi])) { fi++; while (si < input.length && /\s/.test(input[si])) si++; continue; }
        if (si >= input.length || fmt[fi] !== input[si]) return matched || -1;
        fi++; si++; continue;
      }
      fi++; // skip %
      if (fi >= fmt.length) break;
      if (fmt[fi] === '%') { if (si < input.length && input[si] === '%') { fi++; si++; continue; } else return matched; }

      // Optional '*' (assignment suppression)
      let suppress = false;
      if (fmt[fi] === '*') { suppress = true; fi++; }

      // Optional width
      let maxWidth = 0;
      while (fi < fmt.length && fmt[fi] >= '0' && fmt[fi] <= '9') { maxWidth = maxWidth * 10 + (fmt[fi] - '0'); fi++; }

      // Length modifier (h, hh, l, ll, L, z, j, t)
      let lenMod = '';
      if (fi < fmt.length && 'hlLzjt'.includes(fmt[fi])) {
        lenMod = fmt[fi++];
        if (fi < fmt.length && ((lenMod === 'h' && fmt[fi] === 'h') || (lenMod === 'l' && fmt[fi] === 'l'))) {
          lenMod += fmt[fi++];
        }
      }

      const spec = fmt[fi++];
      const ptr = suppress ? null : argPtrs[ai++];

      // %n doesn't consume input
      if (spec === 'n') {
        if (ptr != null) this.mem.writeInt32(ptr, si);
        continue;
      }

      if (si >= input.length) break;

      // Skip leading whitespace for d, f, s (not c)
      if (spec !== 'c') { while (si < input.length && /\s/.test(input[si])) si++; }
      if (si >= input.length) break;

      switch (spec) {
        case 'd': case 'i': {
          let numStr = '';
          let limit = maxWidth || Infinity;
          if ((input[si] === '-' || input[si] === '+') && limit > 0) { numStr += input[si++]; limit--; }
          while (si < input.length && limit > 0 && input[si] >= '0' && input[si] <= '9') { numStr += input[si++]; limit--; }
          if (numStr === '' || numStr === '-' || numStr === '+') return matched || -1;
          if (ptr != null) {
            if (lenMod === 'll') this.mem.writeBigInt64(ptr, BigInt(numStr));
            else this.mem.writeInt32(ptr, parseInt(numStr, 10));
          }
          matched++;
          break;
        }
        case 'u': {
          let numStr = '';
          let limit = maxWidth || Infinity;
          while (si < input.length && limit > 0 && input[si] >= '0' && input[si] <= '9') { numStr += input[si++]; limit--; }
          if (numStr === '') return matched || -1;
          if (ptr != null) {
            if (lenMod === 'll') this.mem.writeBigUint64(ptr, BigInt(numStr));
            else this.mem.writeUint32(ptr, parseInt(numStr, 10) >>> 0);
          }
          matched++;
          break;
        }
        case 'x': case 'X': {
          let numStr = '';
          let limit = maxWidth || Infinity;
          if (si + 1 < input.length && input[si] === '0' && (input[si+1] === 'x' || input[si+1] === 'X') && limit > 2) { si += 2; limit -= 2; }
          while (si < input.length && limit > 0 && /[0-9a-fA-F]/.test(input[si])) { numStr += input[si++]; limit--; }
          if (numStr === '') return matched || -1;
          if (ptr != null) this.mem.writeUint32(ptr, parseInt(numStr, 16) >>> 0);
          matched++;
          break;
        }
        case 'f': case 'e': case 'g': case 'E': case 'G': {
          let numStr = '';
          let limit = maxWidth || Infinity;
          if ((input[si] === '-' || input[si] === '+') && limit > 0) { numStr += input[si++]; limit--; }
          let hasDot = false, hasExp = false;
          while (si < input.length && limit > 0) {
            if (input[si] >= '0' && input[si] <= '9') { numStr += input[si++]; limit--; }
            else if (input[si] === '.' && !hasDot && !hasExp) { hasDot = true; numStr += input[si++]; limit--; }
            else if ((input[si] === 'e' || input[si] === 'E') && !hasExp && numStr.length > 0 && numStr !== '-' && numStr !== '+') {
              hasExp = true; numStr += input[si++]; limit--;
              if (si < input.length && limit > 0 && (input[si] === '-' || input[si] === '+')) { numStr += input[si++]; limit--; }
            }
            else break;
          }
          if (numStr === '' || numStr === '-' || numStr === '+') return matched || -1;
          if (ptr != null) this.mem.writeFloat64(ptr, parseFloat(numStr));
          matched++;
          break;
        }
        case 's': {
          let str = '';
          let limit = maxWidth || Infinity;
          while (si < input.length && limit > 0 && !/\s/.test(input[si])) { str += input[si++]; limit--; }
          if (str === '') return matched || -1;
          if (ptr != null) this.mem.writeString(ptr, str);
          matched++;
          break;
        }
        case 'c': {
          if (ptr != null) this.mem.writeUint8(ptr, input.charCodeAt(si) & 0xFF);
          si++;
          matched++;
          break;
        }
        default:
          return matched;
      }
    }
    return matched === 0 && si >= input.length ? -1 : matched;
  }

  // ======================== string functions =================================
  strlen(addr) {
    let len = 0;
    while (this.mem.u8[addr + len] !== 0) len++;
    return len;
  }

  strcpy(dest, src) {
    let i = 0;
    while (true) {
      const c = this.mem.u8[src + i];
      this.mem.u8[dest + i] = c;
      if (c === 0) break;
      i++;
    }
    return dest;
  }

  strncpy(dest, src, n) {
    let i = 0;
    for (; i < n; i++) {
      const c = this.mem.u8[src + i];
      this.mem.u8[dest + i] = c;
      if (c === 0) break;
    }
    for (; i < n; i++) this.mem.u8[dest + i] = 0;
    return dest;
  }

  strcmp(a, b) {
    let i = 0;
    while (true) {
      const ca = this.mem.u8[a + i];
      const cb = this.mem.u8[b + i];
      if (ca !== cb) return ca < cb ? -1 : 1;
      if (ca === 0) return 0;
      i++;
    }
  }

  strncmp(a, b, n) {
    for (let i = 0; i < n; i++) {
      const ca = this.mem.u8[a + i];
      const cb = this.mem.u8[b + i];
      if (ca !== cb) return ca < cb ? -1 : 1;
      if (ca === 0) return 0;
    }
    return 0;
  }

  strcat(dest, src) {
    let dlen = this.strlen(dest);
    this.strcpy(dest + dlen, src);
    return dest;
  }

  strncat(dest, src, n) {
    let dlen = this.strlen(dest);
    let i = 0;
    for (; i < n; i++) {
      const c = this.mem.u8[src + i];
      if (c === 0) break;
      this.mem.u8[dest + dlen + i] = c;
    }
    this.mem.u8[dest + dlen + i] = 0;
    return dest;
  }

  strchr(addr, ch) {
    ch = ch & 0xFF;
    for (let i = addr; ; i++) {
      const c = this.mem.u8[i];
      if (c === ch) return i;
      if (c === 0) return 0;
    }
  }

  strrchr(addr, ch) {
    ch = ch & 0xFF;
    let last = 0;
    for (let i = addr; ; i++) {
      const c = this.mem.u8[i];
      if (c === ch) last = i;
      if (c === 0) break;
    }
    return last;
  }

  strstr(haystack, needle) {
    const needleStr = this.mem.readString(needle);
    if (needleStr.length === 0) return haystack;
    const haystackStr = this.mem.readString(haystack);
    const idx = haystackStr.indexOf(needleStr);
    return idx === -1 ? 0 : haystack + idx;
  }

  memcpy(dest, src, n) {
    // Use a temporary copy to handle non-overlapping correctly
    const tmp = new Uint8Array(n);
    tmp.set(this.mem.u8.subarray(src, src + n));
    this.mem.u8.set(tmp, dest);
    return dest;
  }

  memmove(dest, src, n) {
    if (dest < src) {
      for (let i = 0; i < n; i++) this.mem.u8[dest + i] = this.mem.u8[src + i];
    } else if (dest > src) {
      for (let i = n - 1; i >= 0; i--) this.mem.u8[dest + i] = this.mem.u8[src + i];
    }
    return dest;
  }

  memset(dest, val, n) {
    this.mem.u8.fill(val & 0xFF, dest, dest + n);
    return dest;
  }

  memcmp(a, b, n) {
    for (let i = 0; i < n; i++) {
      const ca = this.mem.u8[a + i];
      const cb = this.mem.u8[b + i];
      if (ca !== cb) return ca < cb ? -1 : 1;
    }
    return 0;
  }

  memchr(addr, val, n) {
    val = val & 0xFF;
    for (let i = 0; i < n; i++) {
      if (this.mem.u8[addr + i] === val) return addr + i;
    }
    return 0;
  }

  // ======================== stdlib functions =================================
  atoi(addr) {
    return parseInt(this.mem.readString(addr), 10) || 0;
  }

  atof(addr) {
    return parseFloat(this.mem.readString(addr)) || 0;
  }

  strtol(addr, endPtrAddr, base) {
    const s = this.mem.readString(addr);
    let i = 0;
    while (i < s.length && /\s/.test(s[i])) i++;
    let neg = false;
    if (s[i] === '-') { neg = true; i++; } else if (s[i] === '+') { i++; }
    if (base === 0) {
      if (s[i] === '0' && (s[i + 1] === 'x' || s[i + 1] === 'X')) { base = 16; i += 2; }
      else if (s[i] === '0') { base = 8; i++; }
      else base = 10;
    } else if (base === 16 && s[i] === '0' && (s[i + 1] === 'x' || s[i + 1] === 'X')) {
      i += 2;
    }
    let val = 0;
    let start = i;
    while (i < s.length) {
      let d = digitVal(s[i], base);
      if (d < 0) break;
      val = val * base + d;
      i++;
    }
    if (endPtrAddr) this.mem.writeInt32(endPtrAddr, addr + i);
    val = neg ? -val : val;
    return val | 0;
  }

  strtoul(addr, endPtrAddr, base) {
    const val = this.strtol(addr, endPtrAddr, base);
    return val >>> 0;
  }

  strtoll(addr, endPtrAddr, base) {
    return this.strtol(addr, endPtrAddr, base);
  }

  strtoull(addr, endPtrAddr, base) {
    return this.strtoul(addr, endPtrAddr, base);
  }

  strdup(addr) {
    const len = this.strlen(addr);
    const ptr = this.malloc(len + 1);
    if (ptr) this.memcpy(ptr, addr, len + 1);
    return ptr;
  }

  strtod(addr, endPtrAddr) {
    const s = this.mem.readString(addr);
    const val = parseFloat(s);
    if (endPtrAddr) {
      // Find how many chars consumed
      let match = s.match(/^\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/);
      let consumed = match ? match[0].length : 0;
      this.mem.writeInt32(endPtrAddr, addr + consumed);
    }
    return isNaN(val) ? 0 : val;
  }

  abs(n) { return Math.abs(n | 0); }
  labs(n) { return Math.abs(n | 0); }

  __errno_ptr() { return this._errnoAddr; }

  rand() {
    // LCG pseudo-random number generator
    this._randState = ((this._randState * 1103515245) + 12345) & 0x7FFFFFFF;
    return this._randState;
  }

  srand(seed) {
    this._randState = seed >>> 0;
  }

  exit(code) {
    this._exitCode = code;
    throw new ExitException(code);
  }

  abort() {
    this._writeStderr('Aborted\n');
    this.exit(134);
  }

  // ======================== setjmp / longjmp ==================================
  _setjmp(envPtr) {
    const id = ++this._setjmpCounter;
    this.mem.writeInt32(envPtr, id);
    return id;
  }

  longjmp(envPtr, val) {
    const id = this.mem.readInt32(envPtr);
    throw new LongjmpException(id, val || 1); // C standard: longjmp(env,0) → 1
  }

  // ======================== signal (stub) =====================================
  signal(sig, handler) {
    return 0; // SIG_DFL — signal handling not supported in JS environment
  }

  perror(addr) {
    const prefix = addr ? this.mem.readString(addr) : '';
    if (prefix) this._writeStderr(prefix + ': error\n');
    else this._writeStderr('error\n');
  }

  qsort(baseAddr, numItems, itemSize, compareFn) {
    // Read items into a JS array, sort, write back
    const items = [];
    for (let i = 0; i < numItems; i++) {
      const addr = baseAddr + i * itemSize;
      items.push(this.mem.u8.slice(addr, addr + itemSize));
    }
    items.sort((a, b) => {
      // Write a and b into temporary memory locations for the comparator
      const tmpA = this.mem.malloc(itemSize);
      const tmpB = this.mem.malloc(itemSize);
      this.mem.u8.set(a, tmpA);
      this.mem.u8.set(b, tmpB);
      const result = compareFn(tmpA, tmpB);
      this.mem.free(tmpA);
      this.mem.free(tmpB);
      return result;
    });
    for (let i = 0; i < numItems; i++) {
      this.mem.u8.set(items[i], baseAddr + i * itemSize);
    }
  }

  bsearch(keyAddr, baseAddr, numItems, itemSize, compareFn) {
    let lo = 0, hi = numItems - 1;
    while (lo <= hi) {
      const mid = (lo + hi) >>> 1;
      const midAddr = baseAddr + mid * itemSize;
      const cmp = compareFn(keyAddr, midAddr);
      if (cmp === 0) return midAddr;
      if (cmp < 0) hi = mid - 1;
      else lo = mid + 1;
    }
    return 0;
  }

  // ======================== math functions ===================================
  sin(x)        { return Math.sin(x); }
  cos(x)        { return Math.cos(x); }
  tan(x)        { return Math.tan(x); }
  asin(x)       { return Math.asin(x); }
  acos(x)       { return Math.acos(x); }
  atan(x)       { return Math.atan(x); }
  atan2(y, x)   { return Math.atan2(y, x); }
  sqrt(x)       { return Math.sqrt(x); }
  pow(x, y)     { return Math.pow(x, y); }
  fabs(x)       { return Math.abs(x); }
  ceil(x)       { return Math.ceil(x); }
  floor(x)      { return Math.floor(x); }
  fmod(x, y)    { return x % y; }
  log(x)        { return Math.log(x); }
  log10(x)      { return Math.log10(x); }
  exp(x)        { return Math.exp(x); }

  ldexp(x, exp) {
    return x * Math.pow(2, exp);
  }

  frexp(x, expPtr) {
    if (x === 0) { this.mem.writeInt32(expPtr, 0); return 0; }
    const absX = Math.abs(x);
    let exp = Math.ceil(Math.log2(absX));
    let mantissa = x / Math.pow(2, exp);
    // Normalize so that 0.5 <= |mantissa| < 1.0
    if (Math.abs(mantissa) >= 1.0) { mantissa /= 2; exp++; }
    if (Math.abs(mantissa) < 0.5) { mantissa *= 2; exp--; }
    this.mem.writeInt32(expPtr, exp);
    return mantissa;
  }

  // ======================== ctype functions ==================================
  isalpha(c)  { return ((c >= 65 && c <= 90) || (c >= 97 && c <= 122)) ? 1 : 0; }
  isdigit(c)  { return (c >= 48 && c <= 57) ? 1 : 0; }
  isalnum(c)  { return (this.isalpha(c) || this.isdigit(c)) ? 1 : 0; }
  isspace(c)  { return (c === 32 || (c >= 9 && c <= 13)) ? 1 : 0; }
  isupper(c)  { return (c >= 65 && c <= 90) ? 1 : 0; }
  islower(c)  { return (c >= 97 && c <= 122) ? 1 : 0; }
  ispunct(c)  { return (c >= 33 && c <= 126 && !this.isalnum(c)) ? 1 : 0; }
  isprint(c)  { return (c >= 32 && c <= 126) ? 1 : 0; }
  iscntrl(c)  { return (c < 32 || c === 127) ? 1 : 0; }
  isxdigit(c) { return ((c >= 48 && c <= 57) || (c >= 65 && c <= 70) || (c >= 97 && c <= 102)) ? 1 : 0; }
  toupper(c)  { return (c >= 97 && c <= 122) ? c - 32 : c; }
  tolower(c)  { return (c >= 65 && c <= 90) ? c + 32 : c; }

  // ======================== File I/O ========================================
  fopen(filenameAddr, modeAddr) {
    const filename = this.mem.readString(filenameAddr);
    const mode = this.mem.readString(modeAddr);
    if (!fs) return 0;

    let flags;
    switch (mode.replace('b', '')) {
      case 'r':   flags = 'r';  break;
      case 'r+':  flags = 'r+'; break;
      case 'w':   flags = 'w';  break;
      case 'w+':  flags = 'w+'; break;
      case 'a':   flags = 'a';  break;
      case 'a+':  flags = 'a+'; break;
      default:    return 0;
    }

    try {
      const fd = fs.openSync(filename, flags);
      const ptr = this._nextFd + 10; // use sentinel pointer values >3
      this._filePtrToFd.set(ptr, fd);
      this._fdToFilePtr.set(fd, ptr);
      this._files.set(fd, { fd, filename, mode, eof: false, pos: 0 });
      this._nextFd++;
      return ptr;
    } catch (e) {
      return 0;
    }
  }

  fclose(filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined || fd < 3) return -1;
    try {
      fs.closeSync(fd);
    } catch (e) { /* ignore */ }
    this._filePtrToFd.delete(filePtr);
    this._fdToFilePtr.delete(fd);
    this._files.delete(fd);
    return 0;
  }

  // ======================== POSIX-style File I/O (open/read/close) ===========
  open(pathnameAddr, flags) {
    if (!fs) return -1;
    const pathname = this.mem.readString(pathnameAddr);
    try {
      let nodeFlags = 'r';
      if (flags & 1) nodeFlags = (flags & 2) ? 'r+' : 'w';
      else if (flags & 2) nodeFlags = 'r+';
      const fd = fs.openSync(pathname, nodeFlags);
      return fd;
    } catch (e) {
      return -1;
    }
  }

  read(fd, bufAddr, count) {
    if (!fs) return -1;
    try {
      const tempBuf = Buffer.alloc(count);
      const bytesRead = fs.readSync(fd, tempBuf, 0, count);
      for (let i = 0; i < bytesRead; i++) {
        this.mem.u8[bufAddr + i] = tempBuf[i];
      }
      return bytesRead;
    } catch (e) {
      return -1;
    }
  }

  close(fd) {
    if (!fs) return -1;
    try {
      fs.closeSync(fd);
      return 0;
    } catch (e) {
      return -1;
    }
  }

  fread(bufAddr, elemSize, count, filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined) return 0;
    if (fd < 3) return 0; // cannot fread stdin in this simplified runtime

    const totalBytes = elemSize * count;
    const tempBuf = Buffer.alloc(totalBytes);
    try {
      const info = this._files.get(fd);
      const bytesRead = fs.readSync(fd, tempBuf, 0, totalBytes, info.pos);
      info.pos += bytesRead;
      for (let i = 0; i < bytesRead; i++) {
        this.mem.u8[bufAddr + i] = tempBuf[i];
      }
      if (bytesRead < totalBytes) info.eof = true;
      return Math.floor(bytesRead / elemSize);
    } catch (e) {
      return 0;
    }
  }

  fwrite(bufAddr, elemSize, count, filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined) return 0;

    const totalBytes = elemSize * count;

    if (filePtr === this.STDOUT_PTR) {
      let s = '';
      for (let i = 0; i < totalBytes; i++) s += String.fromCharCode(this.mem.u8[bufAddr + i]);
      this._writeStdout(s);
      return count;
    }
    if (filePtr === this.STDERR_PTR) {
      let s = '';
      for (let i = 0; i < totalBytes; i++) s += String.fromCharCode(this.mem.u8[bufAddr + i]);
      this._writeStderr(s);
      return count;
    }

    try {
      const info = this._files.get(fd);
      const tempBuf = Buffer.from(this.mem.u8.buffer, bufAddr, totalBytes);
      const written = fs.writeSync(fd, tempBuf, 0, totalBytes, info.pos);
      info.pos += written;
      return Math.floor(written / elemSize);
    } catch (e) {
      return 0;
    }
  }

  fgets(bufAddr, maxLen, filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined) return 0;

    if (fd < 3) {
      // stdin
      const line = this._readStdinLine();
      if (!line && this._stdinEof) return 0;
      const toWrite = line.substring(0, maxLen - 1);
      this.mem.writeString(bufAddr, toWrite);
      return bufAddr;
    }

    try {
      const info = this._files.get(fd);
      const oneBuf = Buffer.alloc(1);
      let i = 0;
      while (i < maxLen - 1) {
        const bytesRead = fs.readSync(fd, oneBuf, 0, 1, info.pos);
        if (bytesRead === 0) { info.eof = true; break; }
        info.pos += 1;
        const ch = oneBuf[0];
        this.mem.u8[bufAddr + i] = ch;
        i++;
        if (ch === 10) break; // newline
      }
      if (i === 0) return 0;
      this.mem.u8[bufAddr + i] = 0;
      return bufAddr;
    } catch (e) {
      return 0;
    }
  }

  fputs(strAddr, filePtr) {
    const s = this.mem.readString(strAddr);
    if (filePtr === this.STDOUT_PTR) { this._writeStdout(s); return s.length; }
    if (filePtr === this.STDERR_PTR) { this._writeStderr(s); return s.length; }
    this._fileWrite(filePtr, s);
    return s.length;
  }

  fgetc(filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined) return -1;
    if (fd < 3) {
      if (this._stdinPos < this._stdinBuf.length) return this._stdinBuf.charCodeAt(this._stdinPos++);
      this._stdinEof = true;
      return -1;
    }
    try {
      const info = this._files.get(fd);
      const oneBuf = Buffer.alloc(1);
      const bytesRead = fs.readSync(fd, oneBuf, 0, 1, info.pos);
      if (bytesRead === 0) { info.eof = true; return -1; }
      info.pos += 1;
      return oneBuf[0];
    } catch (e) {
      return -1;
    }
  }

  fputc(ch, filePtr) {
    const s = String.fromCharCode(ch & 0xFF);
    if (filePtr === this.STDOUT_PTR) { this._writeStdout(s); return ch & 0xFF; }
    if (filePtr === this.STDERR_PTR) { this._writeStderr(s); return ch & 0xFF; }
    this._fileWrite(filePtr, s);
    return ch & 0xFF;
  }

  feof(filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined) return 1;
    if (fd < 3) return this._stdinEof ? 1 : 0;
    const info = this._files.get(fd);
    return (info && info.eof) ? 1 : 0;
  }

  fseek(filePtr, offset, whence) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined || fd < 3) return -1;
    const info = this._files.get(fd);
    if (!info) return -1;
    const SEEK_SET = 0, SEEK_CUR = 1, SEEK_END = 2;
    try {
      if (whence === SEEK_SET) {
        info.pos = offset;
      } else if (whence === SEEK_CUR) {
        info.pos += offset;
      } else if (whence === SEEK_END) {
        const stat = fs.fstatSync(fd);
        info.pos = stat.size + offset;
      } else {
        return -1;
      }
      info.eof = false;
      return 0;
    } catch (e) {
      return -1;
    }
  }

  ftell(filePtr) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined || fd < 3) return -1;
    const info = this._files.get(fd);
    return info ? info.pos : -1;
  }

  rewind(filePtr) {
    this.fseek(filePtr, 0, 0);
  }

  fflush(filePtr) {
    /* No-op in JS - output is already unbuffered */
    return 0;
  }

  // Convenience accessors for stdin/stdout/stderr pointers
  get stdin()  { return this.STDIN_PTR; }
  get stdout() { return this.STDOUT_PTR; }
  get stderr() { return this.STDERR_PTR; }

  // ======================== getchar / putchar / puts =========================
  getc(filePtr) { return this.fgetc(filePtr); }
  putc(ch, filePtr) { return this.fputc(ch, filePtr); }

  gets(bufAddr) {
    // Read a line from stdin (deprecated C function, but needed for compatibility)
    const line = this._readStdinLine();
    if (!line && this._stdinEof) return 0;
    // Remove trailing newline (gets doesn't include it)
    const trimmed = line.replace(/\n$/, '');
    this.mem.writeString(bufAddr, trimmed);
    return bufAddr;
  }

  freopen(filenameAddr, modeAddr, filePtr) {
    // When stdin is reopened after EOF (e.g., freopen("/dev/tty","r",stdin)),
    // exit gracefully since there's no terminal to reopen in piped context
    if (filePtr === this.STDIN_PTR && this._stdinEof) {
      process.exit(0);
    }
    if (filePtr === this.STDIN_PTR || filePtr === this.STDOUT_PTR || filePtr === this.STDERR_PTR)
      return filePtr;
    return 0;
  }

  getchar() {
    // Read one char from stdin (synchronous)
    if (this._stdinPos < this._stdinBuf.length) {
      return this._stdinBuf.charCodeAt(this._stdinPos++);
    }
    // Try to read more from stdin
    if (fs) {
      try {
        const buf = Buffer.alloc(1);
        const bytesRead = fs.readSync(0, buf, 0, 1);
        if (bytesRead === 0) { this._stdinEof = true; return -1; }
        return buf[0];
      } catch (e) {
        this._stdinEof = true;
        return -1;
      }
    }
    this._stdinEof = true;
    return -1;
  }

  putchar(ch) {
    this._writeStdout(String.fromCharCode(ch & 0xFF));
    return ch & 0xFF;
  }

  puts(addr) {
    const s = this.mem.readString(addr);
    this._writeStdout(s + '\n');
    return s.length + 1;
  }

  // ======================== assert ==========================================
  assert(expr, msgAddr) {
    if (!expr) {
      const msg = msgAddr ? this.mem.readString(msgAddr) : 'assertion failed';
      this._writeStderr('Assertion failed: ' + msg + '\n');
      this.abort();
    }
  }

  // ======================== time functions ===================================
  clock() {
    // Returns microseconds-ish (CLOCKS_PER_SEC = 1000000 in many impls)
    return ((Date.now() - this._startTime) * 1000) | 0;
  }

  time(ptrAddr) {
    const t = Math.floor(Date.now() / 1000);
    if (ptrAddr) this.mem.writeInt32(ptrAddr, t);
    return t;
  }

  difftime(t1, t0) {
    return t1 - t0;
  }

  localtime(ptrAddr) {
    const t = this.mem.readInt32(ptrAddr);
    const d = new Date(t * 1000);
    // Allocate a struct tm (36 bytes) on heap
    const tm = this.malloc(36);
    this.mem.writeInt32(tm + 0, d.getSeconds());      // tm_sec
    this.mem.writeInt32(tm + 4, d.getMinutes());      // tm_min
    this.mem.writeInt32(tm + 8, d.getHours());        // tm_hour
    this.mem.writeInt32(tm + 12, d.getDate());        // tm_mday
    this.mem.writeInt32(tm + 16, d.getMonth());       // tm_mon
    this.mem.writeInt32(tm + 20, d.getFullYear() - 1900); // tm_year
    this.mem.writeInt32(tm + 24, d.getDay());         // tm_wday
    this.mem.writeInt32(tm + 28, 0);                  // tm_yday (approx)
    this.mem.writeInt32(tm + 32, -1);                 // tm_isdst
    return tm;
  }

  strftime(bufAddr, maxLen, fmtAddr, tmAddr) {
    const fmt = this.mem.readString(fmtAddr);
    const sec  = this.mem.readInt32(tmAddr + 0);
    const min  = this.mem.readInt32(tmAddr + 4);
    const hour = this.mem.readInt32(tmAddr + 8);
    const mday = this.mem.readInt32(tmAddr + 12);
    const mon  = this.mem.readInt32(tmAddr + 16);
    const year = this.mem.readInt32(tmAddr + 20);
    const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
    let s = fmt;
    s = s.replace(/%Y/g, String(year + 1900));
    s = s.replace(/%m/g, String(mon + 1).padStart(2, '0'));
    s = s.replace(/%d/g, String(mday).padStart(2, '0'));
    s = s.replace(/%H/g, String(hour).padStart(2, '0'));
    s = s.replace(/%M/g, String(min).padStart(2, '0'));
    s = s.replace(/%S/g, String(sec).padStart(2, '0'));
    s = s.replace(/%b/g, months[mon] || '???');
    if (s.length < maxLen) {
      this.mem.writeString(bufAddr, s);
      return s.length;
    }
    return 0;
  }

  // ======================== I/O helpers (internal) ===========================
  _writeStdout(s) {
    this._stdout += s;
    if (typeof process !== 'undefined' && process.stdout) {
      process.stdout.write(s);
    }
  }

  _writeStderr(s) {
    this._stderr += s;
    if (typeof process !== 'undefined' && process.stderr) {
      process.stderr.write(s);
    }
  }

  _readStdinLine() {
    // In a Node.js context, try synchronous read from stdin
    if (this._stdinPos < this._stdinBuf.length) {
      const nlIdx = this._stdinBuf.indexOf('\n', this._stdinPos);
      if (nlIdx >= 0) {
        const line = this._stdinBuf.substring(this._stdinPos, nlIdx + 1);
        this._stdinPos = nlIdx + 1;
        return line;
      }
      const rest = this._stdinBuf.substring(this._stdinPos);
      this._stdinPos = this._stdinBuf.length;
      return rest;
    }
    // Try synchronous stdin read (Node.js)
    if (fs) {
      try {
        const buf = Buffer.alloc(4096);
        const bytesRead = fs.readSync(0, buf, 0, 4096);
        if (bytesRead === 0) { this._stdinEof = true; return ''; }
        const data = buf.toString('utf8', 0, bytesRead);
        this._stdinBuf += data;
        return this._readStdinLine();
      } catch (e) {
        this._stdinEof = true;
        return '';
      }
    }
    this._stdinEof = true;
    return '';
  }

  _fileWrite(filePtr, s) {
    const fd = this._filePtrToFd.get(filePtr);
    if (fd === undefined || fd < 3) return;
    const info = this._files.get(fd);
    if (!info) return;
    try {
      const buf = Buffer.from(s, 'utf8');
      const written = fs.writeSync(fd, buf, 0, buf.length, info.pos);
      info.pos += written;
    } catch (e) { /* ignore */ }
  }

  // ======================== programmatic stdin feed ==========================
  feedStdin(data) {
    this._stdinBuf += data;
  }

  // ======================== output retrieval =================================
  getStdout() { return this._stdout; }
  getStderr() { return this._stderr; }
}

// ---------------------------------------------------------------------------
// Exit exception used by exit() / abort()
// ---------------------------------------------------------------------------
class ExitException extends Error {
  constructor(code) {
    super(`exit(${code})`);
    this.name = 'ExitException';
    this.code = code;
  }
}

// ---------------------------------------------------------------------------
// LongjmpException used by longjmp()
// ---------------------------------------------------------------------------
class LongjmpException extends Error {
  constructor(id, val) {
    super(`longjmp(${val})`);
    this.name = 'LongjmpException';
    this.id = id;
    this.val = val;
  }
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
function digitVal(ch, base) {
  let d;
  if (ch >= '0' && ch <= '9') d = ch.charCodeAt(0) - 48;
  else if (ch >= 'a' && ch <= 'f') d = ch.charCodeAt(0) - 97 + 10;
  else if (ch >= 'A' && ch <= 'F') d = ch.charCodeAt(0) - 65 + 10;
  else return -1;
  return d < base ? d : -1;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
Runtime.ExitException = ExitException;
Runtime.LongjmpException = LongjmpException;
Runtime.Memory = Memory;

module.exports = { Runtime, ExitException, LongjmpException, Memory };
