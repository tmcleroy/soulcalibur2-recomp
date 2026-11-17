// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. From cps2recomp's online/ (part of cps2recomp), for Soulcalibur II Recomp.
//
// The introduction service for online play (port/netplay.h, online/README.md). It only introduces: a host asks for a
// code and says where it can be reached (its addresses as the internet and its own network see them); the other
// player gives the code and where they can be reached, and is told the host's; the host, asking again, is told
// theirs. The two then reach each other directly; nothing of the match goes through here. A code lasts 30 minutes,
// or until it has been joined. Every match is a Durable Object of its own, named by its code.
//
//   POST /v1/host                {game, build, addresses}          -> {code, secret}
//   POST /v1/host                {code, secret, game, build, addresses} -> {code, secret}   (the same code again, for
//                                                                  the player who left to come back to: Netplay's reconnect)
//   GET  /v1/host/CODE?secret=S                                    -> {joined: false} | {joined: true, addresses}
//   POST /v1/join/CODE           {game, build, addresses}          -> {addresses} | 404 | 409 {error}
//   GET  /v1/lobby/GAME?protocol=N                                 -> {matches: [{code, name, waiting}]}
//   GET  /v1/code/CODE                                             -> {game, title, version, protocol} | 404 (which game a code is
//                                                                     for, while it can be joined: the launcher's Join)
//   GET  /v1/lobby?protocol=N                                      -> {matches: [{game, title, version, code, name, waiting}]}
//   POST /v1/room                {secret, name, country, games, protocol, version, invitable, blocked, lobby}
//                                                                  -> {id, players: [{id, name, games, country}], invitations: [{code, from,
//                                                                     name, game, title, version}], matches}   (the waiting room:
//                                                                     below; with `lobby: true`, also every game's open matches,
//                                                                     as GET /v1/lobby says them: one request for the Online page)
//   POST /v1/room/leave          {secret}                          -> {}
//   POST /v1/room/decline        {secret, code}                    -> {}
//   GET  /v1/stats?key=K                                           -> a page of numbers, for the service's keeper (STATS_KEY)
//
// A host that asks for it (`listed`, with a `name` and its `protocol`) is in the lobby, a Durable Object (Lobby) that
// keeps every game's: there while it is asking whether someone has joined (seen in the last 25 seconds), and until
// someone has. A match tells the lobby it is still there at most every 10 seconds, however often its host asks.
//
// The waiting room (Room, a Durable Object) is the players whose launcher has its Online page open, each checking in
// every 10 to 20 seconds, and gone 30 seconds after the last. A player is shown to the others with a name, and while they
// let others invite them; never their addresses. A player is known by an id the service makes from a secret only
// their launcher has (so nobody can check in as them), and not kept anywhere but the Room's memory. A host may name
// a player in the room to invite (`invite`, with its own `from` id and `name`): the invitation waits in the room for
// that player's next check-in while the host asks about its match, until someone joins it, or the player declines
// (the host's next question is answered `invite: "declined"`), or it can't be given (`invite: "away"`). A player
// who blocks another (`blocked`, ids their launcher keeps) sees nothing of them, nor they of the player.
//
// Stats (a Durable Object) counts, by day and game, the matches hosted and joined, and which hosts are waiting now:
// counts only, nothing about who. The page also says how many players have their Online page open (the Room).
//
// `addresses` are "IP:PORT" strings (IPv4, or [IPv6]:PORT), at most 8: each side's own, as STUN and its network say them.

const kAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"; // (no 0 and O, 1 and I and L)
const kLifetime = 30 * 60 * 1000;

function json(body, status = 200) {
  return new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
}

function newCode() {
  const bytes = crypto.getRandomValues(new Uint8Array(6));
  return Array.from(bytes, (b) => kAlphabet[b % kAlphabet.length]).join("");
}

// "K7Q-M2X", "k7qm2x": the code as kept
function cleanCode(text) {
  const code = text.toUpperCase().replace(/[^0-9A-Z]/g, "");
  return code.length === 6 && [...code].every((c) => kAlphabet.includes(c)) ? code : null;
}

// What a client says it can be reached at, checked, with where its request came from (the port its socket was
// seen at from the internet is in the addresses it sends: STUN; the request's own address is its IP only)
function cleanAddresses(list) {
  if (!Array.isArray(list)) {
    return [];
  }
  return list
    .filter((a) => typeof a === "string" && a.length <= 64 && /^(\d{1,3}(\.\d{1,3}){3}|\[[0-9a-fA-F:.]+\]):\d{1,5}$/.test(a))
    .slice(0, 8);
}

function cleanText(text) {
  return typeof text === "string" ? text.slice(0, 64) : "";
}

// A player's name as the lobby shows it: printable, at most 24 characters
function cleanName(text) {
  const name = typeof text === "string" ? text.replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, 24) : "";
  return name || "A player";
}

const kSeen = 25 * 1000; // (a listed host that has not asked for this long is not shown)
const kTell = 10 * 1000; // (a match's host asking: the lobby told it is still there, at most this often)
const kHere = 30 * 1000; // (a player in the waiting room, or an invitation, not seen for this long is gone)

// A waiting room id, "3f9a0c1e2b4d": a player's secret's SHA-256, its first 12 hex digits
async function playerId(secret) {
  if (typeof secret !== "string" || secret.length < 16 || secret.length > 64) {
    return null;
  }
  const hash = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(secret)));
  return Array.from(hash.slice(0, 6), (b) => b.toString(16).padStart(2, "0")).join("");
}

function cleanIds(list) {
  return Array.isArray(list) ? list.filter((id) => typeof id === "string" && /^[0-9a-f]{12}$/.test(id)).slice(0, 200) : [];
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const parts = url.pathname.split("/").filter(Boolean);
    // (a count for the stats, after the answer: it never holds the answer up)
    const count = (what, body) => {
      const stats = env.STATS.get(env.STATS.idFromName("all"));
      ctx.waitUntil(stats.fetch(`https://stats/${what}`, { method: "POST", body: JSON.stringify(body) }).catch(() => {}));
    };
    if (request.method === "GET" && parts[1] === "stats" && parts.length === 2) {
      return statsPage(env, url.searchParams.get("key") || "");
    }
    if (parts[0] !== "v1") {
      // (someone who came here in a browser: to the project)
      return Response.redirect("https://github.com/tmcleroy/soulcalibur2-recomp", 302);
    }
    if (request.method === "POST" && parts[1] === "host" && parts.length === 2) {
      const text = await request.text();
      let asked = null;
      try {
        asked = JSON.parse(text);
      } catch {
        return json({ error: "not JSON" }, 400);
      }
      // (the host's code again: open to be joined once more, if it is theirs, or free)
      const again = typeof asked.code === "string" ? cleanCode(asked.code) : null;
      if (again && typeof asked.secret === "string") {
        const match = env.MATCHES.get(env.MATCHES.idFromName(again));
        const answer = await match.fetch(`https://match/host?code=${again}`, { method: "POST", body: text });
        if (answer.ok) {
          return json({ code: again, secret: asked.secret });
        }
      }
      // (a code not in use: tried again on the rare clash)
      for (let tries = 0; tries < 5; tries++) {
        const code = newCode();
        const match = env.MATCHES.get(env.MATCHES.idFromName(code));
        const answer = await match.fetch(`https://match/host?code=${code}`, { method: "POST", body: JSON.stringify({ ...asked, code: undefined, secret: undefined }) });
        if (answer.status !== 409) {
          const body = await answer.json();
          if (answer.ok) {
            count("hosted", { game: cleanText(asked.game) });
          }
          return json(answer.ok ? { code, secret: body.secret } : body, answer.status);
        }
      }
      return json({ error: "no code free" }, 503);
    }
    if (request.method === "POST" && parts[1] === "room" && parts.length <= 3 && parts[2] !== "count") {
      const room = env.ROOMS.get(env.ROOMS.idFromName("all"));
      const text = await request.text();
      let lobby = null;
      if (!parts[2]) {
        // (a check-in that asks for the lobby too: the two asked at once, and said in one answer)
        try {
          const asked = JSON.parse(text);
          if (asked.lobby === true) {
            lobby = env.LOBBIES.get(env.LOBBIES.idFromName("all")).fetch(`https://lobby/list?protocol=${encodeURIComponent(Number(asked.protocol) || 0)}`);
          }
        } catch {
          // (the room says what was wrong with it)
        }
      }
      const answer = await room.fetch(`https://room/${parts[2] || "here"}`, { method: "POST", body: text });
      if (!lobby || !answer.ok) {
        return answer;
      }
      const listed = await lobby;
      return json({ ...(await answer.json()), matches: listed.ok ? (await listed.json()).matches : [] });
    }
    if (request.method === "GET" && parts[1] === "lobby" && (parts.length === 2 || parts.length === 3)) {
      const lobby = env.LOBBIES.get(env.LOBBIES.idFromName("all"));
      const game = parts.length === 3 ? `&game=${encodeURIComponent(cleanText(parts[2]))}` : "";
      return lobby.fetch(`https://lobby/list?protocol=${encodeURIComponent(url.searchParams.get("protocol") || "")}${game}`);
    }
    const code = parts.length === 3 ? cleanCode(parts[2]) : null;
    if (!code) {
      return json({ error: "no such code" }, 404);
    }
    const match = env.MATCHES.get(env.MATCHES.idFromName(code));
    if (request.method === "GET" && parts[1] === "code") {
      return match.fetch("https://match/info");
    }
    if (request.method === "GET" && parts[1] === "host") {
      return match.fetch(`https://match/poll?secret=${encodeURIComponent(url.searchParams.get("secret") || "")}`);
    }
    if (request.method === "POST" && parts[1] === "join") {
      const text = await request.text();
      const answer = await match.fetch("https://match/join", { method: "POST", body: text });
      if (answer.ok) {
        let game = "";
        try {
          game = cleanText(JSON.parse(text).game);
        } catch {}
        count("joined", { game, code });
      }
      return answer;
    }
    return json({ error: "not here" }, 404);
  },
};

export class Match {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.told = 0; // (when the lobby was last told this match is still there: kTell)
  }

  // (the game's lobby told: added, still there, or gone)
  async lobby(match, what, code) {
    if (!match || !match.listed) {
      return;
    }
    const lobby = this.env.LOBBIES.get(this.env.LOBBIES.idFromName("all"));
    await lobby.fetch(`https://lobby/${what}`, {
      method: "POST",
      body: JSON.stringify({ code, game: match.game, title: match.title, version: match.version, name: match.name, protocol: match.protocol, made: match.made }),
    });
  }

  // (the invited player's room told: the invitation given again, or taken back; what it said)
  async room(match, what) {
    if (!match || !match.invite) {
      return null;
    }
    const room = this.env.ROOMS.get(this.env.ROOMS.idFromName("all"));
    const answer = await room.fetch(`https://room/${what}`, {
      method: "POST",
      body: JSON.stringify({ to: match.invite, from: match.from, code: match.code, game: match.game, title: match.title, version: match.version, protocol: match.protocol,
                             name: match.name }),
    });
    return answer.ok ? answer.json() : null;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    let match = (await this.state.storage.get("match")) || null;
    if (match && now - match.made > kLifetime) {
      await this.state.storage.deleteAll();
      match = null;
    }
    if (url.pathname === "/host") {
      let body;
      try {
        body = JSON.parse(await request.text());
      } catch {
        return json({ error: "not JSON" }, 400);
      }
      // (taken, unless by this host asking for it again; free, it may be had with the secret the host keeps)
      if (match && !(typeof body.secret === "string" && body.secret === match.secret)) {
        return json({ error: "in use" }, 409);
      }
      const secret = typeof body.secret === "string" && /^[0-9a-f-]{36}$/.test(body.secret) ? body.secret : crypto.randomUUID();
      match = { made: now, secret, game: cleanText(body.game), build: cleanText(body.build), host: cleanAddresses(body.addresses), joiner: null,
                listed: body.listed === true, name: cleanName(body.name), title: cleanText(body.title), version: cleanText(body.version),
                protocol: Number(body.protocol) || 0,
                code: url.searchParams.get("code") || "",
                invite: cleanIds([body.invite])[0] || null, from: cleanIds([body.from])[0] || "", invited: "" };
      await this.invite(match);
      await this.state.storage.put("match", match);
      await this.state.storage.setAlarm(now + kLifetime);
      this.told = now;
      await this.lobby(match, "seen", match.code);
      return json({ secret });
    }
    if (!match) {
      return json({ error: "no such code" }, 404);
    }
    if (url.pathname === "/info") {
      if (match.joiner) {
        return json({ error: "Someone has joined that match already." }, 409);
      }
      return json({ game: match.game, title: match.title || match.game, version: match.version || "", protocol: match.protocol });
    }
    if (url.pathname === "/poll") {
      if (url.searchParams.get("secret") !== match.secret) {
        return json({ error: "no such code" }, 404);
      }
      if (!match.joiner && now - (this.waitingSaid || 0) > 15000) {
        // (the stats told this host is waiting, now and then: it is counted as waiting while it has been told lately)
        this.waitingSaid = now;
        const stats = this.env.STATS.get(this.env.STATS.idFromName("all"));
        await stats.fetch("https://stats/waiting", { method: "POST", body: JSON.stringify({ code: match.code, game: match.game, listed: match.listed }) }).catch(() => {});
      }
      if (!match.joiner && now - this.told > kTell) {
        this.told = now;
        await this.lobby(match, "seen", match.code);
      }
      if (!match.joiner) {
        if (match.invite && !match.invited) {
          await this.invite(match);
          if (match.invited) {
            await this.state.storage.put("match", match);
          }
        }
      }
      return json(match.joiner ? { joined: true, addresses: match.joiner } : { joined: false, ...(match.invited ? { invite: match.invited } : {}) });
    }
    if (url.pathname === "/join") {
      let body;
      try {
        body = JSON.parse(await request.text());
      } catch {
        return json({ error: "not JSON" }, 400);
      }
      if (cleanText(body.game) !== match.game) {
        return json({ error: `That code is for another game (${match.game}).` }, 409);
      }
      if (cleanText(body.version) !== (match.version || "")) {
        return json({ error: `That match is on version ${match.version || "an older one"}, and you have ${cleanText(body.version) || "an older one"}: both need the same version.` }, 409);
      }
      if (cleanText(body.build) !== match.build) {
        return json({ error: "The two copies of the game are different builds: use the same release on both." }, 409);
      }
      if (match.joiner) {
        return json({ error: "Someone has joined that match already." }, 409);
      }
      match.joiner = cleanAddresses(body.addresses);
      await this.state.storage.put("match", match);
      await this.lobby(match, "gone", match.code);
      if (!match.invited) {
        await this.room(match, "uninvite");
      }
      return json({ addresses: match.host });
    }
    if (url.pathname === "/declined") {
      // (the invited player said no: the host is told at its next question; the code still joins)
      if (match.invite && !match.invited) {
        match.invited = "declined";
        await this.state.storage.put("match", match);
      }
      return json({});
    }
    return json({ error: "not here" }, 404);
  }

  // (the invitation given, or given again: "away" in `invited` if the player isn't in the room to be given it)
  async invite(match) {
    const said = await this.room(match, "invite");
    if (said && said.given === false) {
      match.invited = "away";
    }
  }

  async alarm() {
    const match = await this.state.storage.get("match");
    await this.lobby(match, "gone", match && match.code);
    if (match && !match.invited) {
      await this.room(match, "uninvite");
    }
    await this.state.storage.deleteAll();
  }
}

// The open matches, every game's: the listed hosts still asking (kSeen), each by its code. Kept in memory only, as the
// waiting room is: a lobby that is let go is made again by the hosts' next questions (each says all the lobby shows).
export class Lobby {
  constructor(state) {
    this.state = state;
    this.matches = {};
  }

  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    const matches = this.matches;
    if (url.pathname === "/list") {
      const protocol = Number(url.searchParams.get("protocol")) || 0;
      const game = url.searchParams.get("game");
      const shown = [];
      for (const [code, m] of Object.entries(matches)) {
        if (now - m.seen > kSeen) {
          delete matches[code];
        } else if (m.protocol === protocol && (!game || m.game === game)) {
          shown.push({ game: m.game, title: m.title || m.game, version: m.version || "", code, name: m.name, waiting: Math.round((now - m.made) / 1000) });
        }
      }
      shown.sort((a, b) => a.waiting - b.waiting);
      return json({ matches: shown.slice(0, 200) });
    }
    const body = JSON.parse(await request.text());
    if (!body.code) {
      return json({});
    }
    if (url.pathname === "/seen") {
      matches[body.code] = { game: body.game, title: body.title, version: body.version, name: body.name, protocol: body.protocol, made: body.made, seen: now };
    } else if (url.pathname === "/gone") {
      delete matches[body.code];
    }
    return json({});
  }
}

// The waiting room: the players whose Online page is open, and the invitations waiting for them (see the top). Kept in
// memory only: a room that is let go (nobody has checked in for a while) is made again by the next check-ins.
export class Room {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.players = new Map(); // id -> {name, country, games, protocol, version, invitable, blocked, seen}
    this.invitations = new Map(); // the invited player's id -> Map(code -> {from, name, game, title, version, seen})
  }

  // (the players and invitations not seen for kHere let go)
  sweep(now) {
    for (const [id, player] of this.players) {
      if (now - player.seen > kHere) {
        this.players.delete(id);
      }
    }
    for (const [id, waiting] of this.invitations) {
      for (const [code, invitation] of waiting) {
        if (now - invitation.seen > kHere) {
          waiting.delete(code);
        }
      }
      if (waiting.size === 0) {
        this.invitations.delete(id);
      }
    }
  }

  // (either of the two has blocked the other)
  apart(a, b) {
    const one = this.players.get(a), other = this.players.get(b);
    return (one && one.blocked.includes(b)) || (other && other.blocked.includes(a));
  }

  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    let body;
    try {
      body = JSON.parse(await request.text());
    } catch {
      return json({ error: "not JSON" }, 400);
    }
    this.sweep(now);
    if (url.pathname === "/count") {
      // (for the stats page: how many have their Online page open, and how many of them with a name)
      let named = 0;
      for (const player of this.players.values()) {
        named += player.name ? 1 : 0;
      }
      return json({ here: this.players.size, named });
    }
    if (url.pathname === "/invite" || url.pathname === "/uninvite") {
      // (from a match: body.to invited to body.code, or no longer)
      const to = cleanIds([body.to])[0];
      const code = typeof body.code === "string" ? body.code : "";
      if (!to || !code) {
        return json({ given: false });
      }
      if (url.pathname === "/uninvite") {
        const waiting = this.invitations.get(to);
        if (waiting) {
          waiting.delete(code);
        }
        return json({});
      }
      const player = this.players.get(to);
      const from = cleanIds([body.from])[0] || "";
      if (!player || !player.invitable || player.protocol !== Number(body.protocol) || player.version !== cleanText(body.version) || this.apart(to, from)) {
        // (not in the room now, not taking invitations, or blocked; once in the room, an invitation stays until it
        // is answered or let go, even when the player turns invitations off meanwhile)
        if (!this.invitations.get(to)?.has(code)) {
          return json({ given: false });
        }
      }
      if (!this.invitations.has(to)) {
        this.invitations.set(to, new Map());
      }
      this.invitations.get(to).set(code, { from, name: cleanName(body.name), game: cleanText(body.game), title: cleanText(body.title), version: cleanText(body.version), seen: now });
      return json({ given: true });
    }
    const id = await playerId(body.secret);
    if (!id) {
      return json({ error: "no secret" }, 400);
    }
    if (url.pathname === "/leave") {
      this.players.delete(id);
      return json({});
    }
    if (url.pathname === "/decline") {
      const code = typeof body.code === "string" ? cleanCode(body.code) : null;
      const waiting = this.invitations.get(id);
      if (code && waiting && waiting.has(code)) {
        waiting.delete(code);
        const match = this.env.MATCHES.get(this.env.MATCHES.idFromName(code));
        await match.fetch("https://match/declined", { method: "POST", body: "{}" });
      }
      return json({});
    }
    if (url.pathname !== "/here") {
      return json({ error: "not here" }, 404);
    }
    // A check-in: this player here (shown with a name), and who else is, and what waits for them
    const name = typeof body.name === "string" ? body.name.replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, 24) : "";
    const me = {
      name,
      // (the country the player chose to show with their name: two letters, or none; never worked out from anything)
      country: typeof body.country === "string" && /^[A-Za-z]{2}$/.test(body.country) ? body.country.toUpperCase() : "",
      games: Array.isArray(body.games) ? body.games.filter((g) => typeof g === "string").map(cleanText).slice(0, 64) : [],
      protocol: Number(body.protocol) || 0,
      version: cleanText(body.version),
      invitable: body.invitable !== false && name !== "",
      blocked: cleanIds(body.blocked),
      seen: now,
    };
    this.players.set(id, me);
    const players = [];
    for (const [other, player] of this.players) {
      if (other !== id && player.invitable && player.protocol === me.protocol && player.version === me.version && !this.apart(id, other)) {
        players.push({ id: other, name: player.name, games: player.games, ...(player.country ? { country: player.country } : {}) });
      }
    }
    players.sort((a, b) => a.name.localeCompare(b.name));
    const invitations = [];
    for (const [code, invitation] of this.invitations.get(id) || []) {
      if (!this.apart(id, invitation.from)) {
        invitations.push({ code, from: invitation.from, name: invitation.name, game: invitation.game, title: invitation.title || invitation.game, version: invitation.version });
      }
    }
    return json({ id, players: players.slice(0, 200), invitations });
  }
}

// The stats: by day (UTC), the matches hosted and joined by game; and the hosts waiting now (told every 15 seconds while
// they wait, forgotten 30 seconds after the last)
export class Stats {
  constructor(state) {
    this.state = state;
    this.waiting = new Map(); // code -> {game, listed, seen}
  }

  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    for (const [code, w] of this.waiting) {
      if (now - w.seen > 30000) {
        this.waiting.delete(code);
      }
    }
    if (url.pathname === "/report") {
      const days = (await this.state.storage.get("days")) || {};
      const waiting = {};
      let listed = 0;
      for (const w of this.waiting.values()) {
        waiting[w.game] = (waiting[w.game] || 0) + 1;
        listed += w.listed ? 1 : 0;
      }
      return json({ days, waiting, listed });
    }
    const body = JSON.parse(await request.text());
    const game = typeof body.game === "string" && body.game ? body.game.slice(0, 32) : "?";
    if (url.pathname === "/waiting") {
      this.waiting.set(String(body.code), { game, listed: body.listed === true, seen: now });
      return json({});
    }
    if (url.pathname === "/hosted" || url.pathname === "/joined") {
      if (url.pathname === "/joined") {
        this.waiting.delete(String(body.code));
      }
      const day = new Date(now).toISOString().slice(0, 10);
      const days = (await this.state.storage.get("days")) || {};
      const today = (days[day] = days[day] || { hosted: {}, joined: {} });
      const kind = url.pathname.slice(1);
      today[kind][game] = (today[kind][game] || 0) + 1;
      // (90 days kept)
      for (const old of Object.keys(days).sort().slice(0, -90)) {
        delete days[old];
      }
      await this.state.storage.put("days", days);
      return json({});
    }
    return json({ error: "not here" }, 404);
  }
}

// The page of numbers, for whoever has the key (a Worker secret: wrangler secret put STATS_KEY)
async function statsPage(env, key) {
  if (!env.STATS_KEY || key !== env.STATS_KEY) {
    return json({ error: "not here" }, 404);
  }
  const stats = await (await env.STATS.get(env.STATS.idFromName("all")).fetch("https://stats/report")).json();
  const room = await (await env.ROOMS.get(env.ROOMS.idFromName("all")).fetch("https://room/count", { method: "POST", body: "{}" })).json();
  const sum = (o) => Object.values(o || {}).reduce((a, b) => a + b, 0);
  const escape = (t) => String(t).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  const waitingNow = sum(stats.waiting);
  const days = Object.keys(stats.days).sort().reverse().slice(0, 14);
  const games = [...new Set(days.flatMap((d) => [...Object.keys(stats.days[d].hosted), ...Object.keys(stats.days[d].joined)]))].sort();
  const rows = days
    .map((d) => `<tr><td>${d}</td><td>${sum(stats.days[d].hosted)}</td><td>${sum(stats.days[d].joined)}</td>${games
      .map((g) => `<td>${stats.days[d].hosted[g] || 0} / ${stats.days[d].joined[g] || 0}</td>`)
      .join("")}</tr>`)
    .join("");
  const html = `<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Soulcalibur II Recomp online</title>
<style>body{font:15px system-ui;margin:24px;background:#16171a;color:#e8e8ea}td,th{padding:4px 10px;text-align:right;border-bottom:1px solid #333}
th:first-child,td:first-child{text-align:left}.big{font-size:28px;font-weight:600}.n{display:inline-block;margin-right:32px}small{color:#999}</style>
<h1>Soulcalibur II Recomp online</h1>
<div class="n"><div class="big">${room.here}</div>Online page open<br><small>${room.named} with a name</small></div>
<div class="n"><div class="big">${waitingNow}</div>hosts waiting<br><small>${stats.listed || 0} of them in the lobby</small></div>
<h2>The last 14 days (UTC)</h2>
<table><tr><th>Day</th><th>Hosted</th><th>Joined</th>${games.map((g) => `<th>${escape(g)}<br><small>hosted / joined</small></th>`).join("")}</tr>${rows || '<tr><td colspan="3">Nothing yet.</td></tr>'}</table>
<p><small>Counts only: nothing about who. A match hosted is a code given; joined, a player let in with it. Refresh for now.</small></p>`;
  return new Response(html, { headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" } });
}

