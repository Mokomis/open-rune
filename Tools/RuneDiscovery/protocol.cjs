// Bounded discovery/control protocol; game credentials never enter DHT records.
const crypto = require('hypercore-crypto')
const b = require('b4a')
const sodium = require('sodium-universal')
const PROTOCOL = 'open-rune-discovery/1'
const GAME_PROTOCOL = 22
const TOPIC = `open-rune/multiplayer/protocol-${GAME_PROTOCOL}/v1`
const KEY = /^[0-9a-f]{64}$/
const MAP = /^(DM-|AR-|HB-)[A-Za-z0-9_-]{1,45}$/
function random(n) { const out = b.alloc(n); sodium.randombytes_buf(out); return out }
const topicKey = text => crypto.hash(b.from(text))
function frame(socket, timeout = 5000) {
  return new Promise((resolve, reject) => {
    let buffer = b.alloc(0), finished = false
    const timer = setTimeout(() => done(new Error('Host did not respond.')), timeout)
    function done(error, value) {
      if (finished) return
      finished = true; clearTimeout(timer)
      socket.removeListener('data', data); socket.removeListener('error', failure); socket.removeListener('close', closed)
      error ? reject(error) : resolve(value)
    }
    function failure() { done(new Error('Connection closed.')) }
    function closed() { done(new Error('Host is unavailable.')) }
    function data(chunk) {
      if (buffer.length + chunk.length > 4096) return done(new Error('Oversized response.'))
      buffer = b.concat([buffer, chunk]); const end = buffer.indexOf(10)
      if (end < 0) return
      if (end !== buffer.length - 1) return done(new Error('Invalid frame.'))
      try { done(null, JSON.parse(b.toString(buffer.subarray(0, end)))) } catch { done(new Error('Invalid response.')) }
    }
    socket.on('data', data).once('error', failure).once('close', closed)
  })
}
function send(socket, value) { socket.write(JSON.stringify(value) + '\n') }
function status(value, nonce, key) {
  if (!value || value.protocol !== PROTOCOL || value.gameProtocol !== GAME_PROTOCOL || value.nonce !== nonce || value.key !== key ||
      !KEY.test(key) || typeof value.name !== 'string' || !/^[\x20-\x7e]{1,48}$/.test(value.name) || !MAP.test(value.map) ||
      !['Deathmatch','Team Deathmatch','Arena','HeadBall'].includes(value.mode) ||
      !Number.isInteger(value.players) || !Number.isInteger(value.capacity) || value.capacity < 2 || value.capacity > 16 ||
      value.players < 0 || value.players > value.capacity || !Number.isInteger(value.available) || value.available < 0 || value.available > value.capacity)
    throw new Error('Incompatible host response.')
  return value
}
function invitation(text) {
  if (typeof text !== 'string' || text.length > 1024) throw new Error('Invalid invitation.')
  const t = text.trim().split(/\s+/)
  const integer = (s, min, max) => /^\d+$/.test(s) && +s >= min && +s <= max
  if (t.length !== 13 || t[0] !== 'OPEN-RUNE-INVITE-4' || !text.startsWith(t[0]+'\n') ||
      !/^\d{1,3}(\.\d{1,3}){3}$/.test(t[1]) || t[1].split('.').some(n=>+n>255) || +t[1].split('.')[0] >=224 || t[1]==='0.0.0.0' ||
      !integer(t[2],1,65535) || !MAP.test(t[3]) || !integer(t[4],1,16) || !integer(t[5],1,4294967295) ||
      !/^[0-9a-f]{32}$/.test(t[6]) || !KEY.test(t[7]) || !integer(t[8],2,16) || +t[4]>+t[8] ||
      !integer(t[9],1,4) || !integer(t[10],2,4) || !integer(t[11],1,4) || !integer(t[12],0,1)) throw new Error('Invalid invitation.')
  return t
}
function encode(t) { return t[0]+'\n'+t[1]+' '+t[2]+'\n'+t.slice(3).join('\n')+'\n' }
module.exports = { PROTOCOL, GAME_PROTOCOL, TOPIC, KEY, random, topicKey, frame, send, status, invitation, encode, b }
