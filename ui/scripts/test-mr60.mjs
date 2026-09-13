import assert from 'node:assert/strict'
import { parseMr60Line } from '../src/mr60bha2Parser.js'

const normal = parseMr60Line('88.00, 16.00, 3.75, NORMAL, NORMAL, 34.44', 1234)
assert.deepEqual(normal, {
  at: 1234,
  heartRateBpm: 88,
  breathRateBrpm: 16,
  breathPeriodSec: 3.75,
  heartStatus: 'NORMAL',
  breathStatus: 'NORMAL',
  distanceCm: 34.44,
})

const partial = parseMr60Line('NO_DATA, 19.00, 3.16, NO_DATA, NORMAL, NO_DATA', 5678)
assert.equal(partial.heartRateBpm, null)
assert.equal(partial.breathRateBrpm, 19)
assert.equal(partial.distanceCm, null)

assert.equal(parseMr60Line('MR60BHA2 Vital Monitor'), null)
assert.equal(parseMr60Line('heart_rate_bpm, breath_rate_brpm, breath_period_sec, heart_status, breath_status, distance_cm'), null)
assert.equal(parseMr60Line('1,2,3'), null)
assert.equal(parseMr60Line('NO_DATA, NO_DATA, NO_DATA, NO_DATA, NO_DATA, NO_DATA'), null)

console.log('✅ MR60BHA2 CSV parser tests passed')
