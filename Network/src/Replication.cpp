#include "openrune/net/Replication.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace openrune::net
{
namespace
{
struct Writer
{
    Bytes b;
    template<class T> void put(T v)
    {
        using U = std::make_unsigned_t<T>;
        U n = std::bit_cast<U>(v);
        for (size_t i = sizeof(T); i; --i) b.push_back(uint8_t(n >> ((i-1)*8)));
    }
    void ref(EntityRef r) { put(r.id); put(r.generation); }
    void header(uint8_t type, uint32_t epoch) { put(uint32_t(0x4f525231)); put(type); put(epoch); }
};
struct Reader
{
    std::span<const uint8_t> b;
    size_t at = 0;
    bool ok = true;
    template<class T> T get()
    {
        if (!ok || b.size()-at < sizeof(T)) { ok = false; return {}; }
        using U = std::make_unsigned_t<T>;
        U v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v = U((v << 8) | b[at++]);
        return std::bit_cast<T>(v);
    }
    EntityRef ref() { auto id = get<uint32_t>(); return {id,get<uint32_t>()}; }
    bool header(uint8_t type, uint32_t epoch)
    { return epoch && get<uint32_t>() == 0x4f525231 && get<uint8_t>() == type && get<uint32_t>() == epoch; }
    bool done() const { return ok && at == b.size(); }
};
// Lossless zero runs reduce the fixed-width entity records before fragmentation.
// The envelope carries an exact bounded output size; it never allocates from an
// unchecked peer length and expansion cannot recurse. Raw snapshots remain valid.
Bytes packSnapshot(std::span<const uint8_t> input)
{
    Writer w; w.put(uint32_t(0x4f525a31)); w.put(uint32_t(input.size()));
    size_t at=0;
    auto zeros=[&](size_t i) { size_t n=0;while(i+n<input.size() && !input[i+n] && n<128)++n;return n; };
    while(at<input.size()) {
        auto n=zeros(at);
        if(n>=3) { w.put(uint8_t(0x80|(n-1)));at+=n;continue; }
        size_t begin=at;
        do { ++at; } while(at<input.size() && at-begin<128 && zeros(at)<3);
        w.put(uint8_t(at-begin-1));w.b.insert(w.b.end(),input.begin()+begin,input.begin()+at);
    }
    return w.b.size()>=31 && w.b.size()+32<input.size()?std::move(w.b):Bytes(input.begin(),input.end());
}
bool unpackSnapshot(std::span<const uint8_t> input,Bytes& output)
{
    if(input.size()<4 || input.size()>MaxSnapshotBytes)return false;
    Reader r{input};if(r.get<uint32_t>()!=0x4f525a31) { output.assign(input.begin(),input.end());return true; }
    auto size=r.get<uint32_t>();if(!r.ok || size<31 || size>MaxSnapshotBytes)return false;
    Bytes result;result.reserve(size);
    while(r.at<input.size()) {
        auto token=r.get<uint8_t>();size_t n=(token&127)+1;
        if(n>size-result.size())return false;
        if(token&128)result.insert(result.end(),n,0);
        else { if(n>input.size()-r.at)return false;result.insert(result.end(),input.begin()+r.at,input.begin()+r.at+n);r.at+=n; }
    }
    if(result.size()!=size)return false;
    output=std::move(result);return true;
}
bool number(uint64_t n) { return n && n < UINT64_MAX; }
bool validRef(EntityRef r) { return bool(r.id) == bool(r.generation); }
bool validEntity(const Entity& e)
{
    if (!e.ref.id || !e.ref.generation || e.kind < EntityKind::Pawn || e.kind > EntityKind::Effect ||
        (e.flags & ~191) || ((e.flags&128) && e.kind!=EntityKind::Weapon) || !e.life || e.animationPhase==INT16_MIN || e.torsoPhase==INT16_MIN ||
        !validRef(e.owner) || !validRef(e.weapon) || !validRef(e.shield) ||
        !validRef(e.combat.offhand) || (e.combat.flags&~15) || e.combat.meter>100 ||
        e.combat.phase>7 || e.combat.age>10000 || e.combat.cooldown>4000 || e.combat.ready>3 ||
        (e.combat.phase>=6 && (e.combat.offhand.id || (e.combat.flags&11) || e.combat.guard)) ||
        e.combat.runeCapacity>200 || e.combat.runePower>e.combat.runeCapacity ||
        (e.kind!=EntityKind::Pawn && e.combat!=NativeCombat{}) ||
        (!e.combat.offhand.id && ((e.combat.flags&3) || e.combat.guard)) ||
        (!e.combat.phase && (e.combat.age || (e.combat.flags&8))) ||
        (e.combat.ready && (e.combat.phase || e.combat.cooldown)) ||
        (e.attachmentJoint!=255 && !e.owner.id)) return false;
    if(e.physical.kind>3 || e.physical.joints.size()>64 || e.physical.basis.size()>64)return false;
    if(!e.physical.kind && e.physical!=PhysicalPose{})return false;
    if((e.physical.kind==1 || e.physical.kind==3) && ((e.kind!=EntityKind::Pawn&&e.kind!=EntityKind::Corpse)||e.physical.joints.empty()||e.physical.rotation!=std::array<int16_t,4>{}))return false;
    if(e.physical.kind==2){int64_t norm=0;for(auto x:e.physical.rotation)norm+=int64_t(x)*x;if(!e.physical.joints.empty()||norm<1000000000||norm>1150000000)return false;}
    if(e.physical.kind!=3 && !e.physical.basis.empty())return false;
    if(e.physical.kind==3) {
        if(e.physical.basis.empty())return false;
        int prior=-1;
        for(const auto& b:e.physical.basis) {
            if(b.joint<=prior || b.joint>=e.physical.joints.size())return false;
            prior=b.joint;
            for(auto scale:b.scale)if(!scale || scale>16384)return false;
        }
    }
    return std::all_of(e.position.begin(),e.position.end(),[](int32_t p){ return p >= -(1<<27) && p <= (1<<27); });
}
void writePhysical(Writer& w,const PhysicalPose& p){w.put(p.kind);if(!p.kind)return;for(auto v:p.root)w.put(v);
    if(p.kind==1 || p.kind==3){w.put(uint8_t(p.joints.size()));for(auto& r:p.joints)for(auto v:r)w.put(v);}
    else for(auto v:p.rotation)w.put(v);
    if(p.kind==3){w.put(uint8_t(p.basis.size()));for(const auto& b:p.basis){w.put(b.joint);for(auto v:b.position)w.put(v);for(auto v:b.scale)w.put(v);}}
}
PhysicalPose readPhysical(Reader& r){PhysicalPose p;p.kind=r.get<uint8_t>();if(!p.kind)return p;if(p.kind>3){r.ok=false;return {};}
    for(auto& v:p.root)v=r.get<int16_t>();if(p.kind==1 || p.kind==3){auto n=r.get<uint8_t>();if(!n||n>64){r.ok=false;return {};}p.joints.resize(n);for(auto& a:p.joints)for(auto& v:a)v=r.get<uint16_t>();}
    else for(auto& v:p.rotation)v=r.get<int16_t>();
    if(p.kind==3){auto n=r.get<uint8_t>();if(!n||n>64){r.ok=false;return {};}p.basis.resize(n);for(auto& b:p.basis){b.joint=r.get<uint8_t>();for(auto& v:b.position)v=r.get<int16_t>();for(auto& v:b.scale)v=r.get<uint16_t>();}}
    return p;
}
void writeEntity(Writer& w,const Entity& e)
{
    w.ref(e.ref); w.put(uint8_t(uint8_t(e.kind)|(e.physical.kind?128:0))); w.put(uint8_t(e.flags|(e.combat!=NativeCombat{}?64:0))); w.put(e.health); w.put(e.life);
    for (auto v:e.position) w.put(v);
    for (auto v:e.velocity) w.put(v);
    for (auto v:e.angles) w.put(v);
    w.put(e.bodyAnimation); w.put(e.torsoAnimation); w.put(e.animationPhase); w.put(e.animationRate);
    w.put(e.torsoPhase);w.put(e.torsoRate);
    w.ref(e.owner); w.ref(e.weapon); w.ref(e.shield);
    w.put(e.attachmentJoint);
    if(e.physical.kind)writePhysical(w,e.physical);
    if(e.combat!=NativeCombat{}) {
        const auto& c=e.combat;w.ref(c.offhand);w.put(c.flags);w.put(c.guard);w.put(c.meter);
        w.put(c.phase);w.put(c.age);w.put(c.cooldown);w.put(c.ready);w.put(c.runePower);w.put(c.runeCapacity);
    }
}
Entity readEntity(Reader& r)
{
    Entity e; e.ref=r.ref();auto type=r.get<uint8_t>();bool hasPhysical=(type&128)!=0;e.kind=EntityKind(type&127); e.flags=r.get<uint8_t>(); e.health=r.get<int16_t>(); e.life=r.get<uint32_t>();
    for (auto& v:e.position) v=r.get<int32_t>();
    for (auto& v:e.velocity) v=r.get<int16_t>();
    for (auto& v:e.angles) v=r.get<uint16_t>();
    e.bodyAnimation=r.get<uint16_t>(); e.torsoAnimation=r.get<uint16_t>();
    e.animationPhase=r.get<int16_t>(); e.animationRate=r.get<int16_t>();
    e.torsoPhase=r.get<int16_t>();e.torsoRate=r.get<int16_t>();
    e.owner=r.ref(); e.weapon=r.ref(); e.shield=r.ref(); e.attachmentJoint=r.get<uint8_t>();if(hasPhysical){e.physical=readPhysical(r);if(!e.physical.kind)r.ok=false;}
    if(e.flags&64) {
        e.flags&=~64;auto& c=e.combat;c.offhand=r.ref();c.flags=r.get<uint8_t>();c.guard=r.get<uint8_t>();
        c.meter=r.get<uint8_t>();c.phase=r.get<uint8_t>();c.age=r.get<uint16_t>();c.cooldown=r.get<uint16_t>();c.ready=r.get<uint8_t>();
        c.runePower=r.get<uint8_t>();c.runeCapacity=r.get<uint8_t>();
        if(c==NativeCombat{})r.ok=false; // Canonical optional representation.
    }
    return e;
}
const Entity* find(const World& w,uint32_t id)
{
    auto it=std::lower_bound(w.begin(),w.end(),id,[](const Entity& e,uint32_t v){return e.ref.id<v;});
    return it!=w.end() && it->ref.id==id ? &*it : nullptr;
}
bool validInput(const Input& i)
{
    return number(i.sequence) && i.createdMs <= ClockSync::MaxTime && i.life &&
        i.forward >= -1000 && i.forward <= 1000 && i.strafe >= -1000 && i.strafe <= 1000 &&
        i.pitch >= -16384 && i.pitch <= 16384 && !(i.held & ~7);
}
bool validAction(const Action& a)
{
    return number(a.id) && number(a.input) && a.life && a.createdMs <= ClockSync::MaxTime &&
        a.kind >= ActionKind::Attack && a.kind <= ActionKind::SelectAxe;
}
bool validBatch(const InputBatch& b)
{
    if (b.inputs.size()>8 || b.actions.size()>4 || (!b.snapshots.receiving && b.snapshots.receivedMask)) return false;
    uint64_t last=0;
    for (auto& i:b.inputs) { if (!validInput(i) || i.sequence<=last) return false; last=i.sequence; }
    last=0;
    for (auto& a:b.actions) { if (!validAction(a) || a.id<=last) return false; last=a.id; }
    return b.snapshots.installed<UINT64_MAX && b.snapshots.receiving<UINT64_MAX;
}
bool validFeedback(const InputFeedback& f)
{
    if (f.highestInput==UINT64_MAX || f.actions.size()>64) return false;
    uint64_t last=0;
    for (auto a:f.actions)
    { if (!number(a.id) || a.id<=last || a.status<Admission::Admitted || a.status>Admission::Cancelled) return false; last=a.id; }
    return true;
}
uint64_t maskFor(size_t count) { return count==64 ? UINT64_MAX : (uint64_t(1)<<count)-1; }
bool validPart(const SnapshotPart& p)
{
    return number(p.id) && p.baseline<p.id && number(p.tick) && p.total>=31 && p.total<=MaxSnapshotBytes &&
        p.count==(p.total+SnapshotPartBytes-1)/SnapshotPartBytes && p.count<=64 && p.index<p.count &&
        p.bytes.size()==std::min(SnapshotPartBytes,size_t(p.total)-size_t(p.index)*SnapshotPartBytes);
}
bool validCombat(const CombatOutcome& c)
{
    if (!number(c.id) || !number(c.tick) || !validRef(c.attacker) || !c.attacker.id || !c.attackerLife ||
        c.move<CombatMove::Kick || c.move>CombatMove::Weapon ||
        c.contact<CombatContact::Miss || c.contact>CombatContact::Hit) return false;
    if (c.contact==CombatContact::Miss)
        return !c.target.id && !c.target.generation && !c.targetLife && !c.targetHealth && !c.staggered;
    if (!validRef(c.target) || !c.target.id || !c.targetLife || c.target==c.attacker) return false;
    return !c.staggered || (c.move==CombatMove::Bash && c.contact==CombatContact::Hit);
}
bool validMatch(const MatchContract& m)
{
    if (m.teams<2 || m.teams>4 || !m.arenaTeamSize || m.arenaTeamSize>8 || m.limbsScore>1 || (m.mode!=GameMode::HeadBall && m.limbsScore) || (m.mode!=GameMode::Arena && m.arenaTeamSize!=1) || (m.mode!=GameMode::Deathmatch && m.mode!=GameMode::TeamDeathmatch && m.mode!=GameMode::Arena && m.mode!=GameMode::HeadBall) || m.tickHz!=60 || m.maxPlayers<2 || m.maxPlayers>32 || !m.rulesRevision ||
        m.map.empty() || m.map.size()>48 ||
        std::all_of(m.gameplayHash.begin(),m.gameplayHash.end(),[](uint8_t b){return b==0;}) ||
        std::all_of(m.mapHash.begin(),m.mapHash.end(),[](uint8_t b){return b==0;})) return false;
    return std::all_of(m.map.begin(),m.map.end(),[](unsigned char c){
        return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='_';
    });
}
bool validActorEvent(const ActorEvent& e)
{
    if(!number(e.id) || !e.ref.id || !e.ref.generation ||
        e.operation<ActorOperation::Spawn || e.operation>ActorOperation::Appearance ||
        e.kind<EntityKind::Pawn || e.kind>EntityKind::Effect)return false;
    if(e.operation==ActorOperation::Appearance) {
        if(!e.package.empty() || !e.actorClass.empty() || !e.proxyPackage.empty() || !e.proxyClass.empty() || !e.mapActor.empty() ||
           (e.appearance.firstGroup!=0 && e.appearance.firstGroup!=8))return false;
        if(e.appearance.scale < -16384 || e.appearance.scale>16384 || e.appearance.drawType>16)return false;
        auto references=std::vector<SkinRef>(e.appearance.skins.begin(),e.appearance.skins.end());references.push_back(e.appearance.skeleton);references.push_back(e.appearance.substitute);
        for(const auto& skin:references) {
            if(skin.package.empty()!=!skin.object || skin.object>INT32_MAX || skin.package.size()>48)return false;
            for(unsigned char c:skin.package)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
        }
        return true;
    }
    if(e.appearance!=ActorAppearance{})return false;
    if(e.operation==ActorOperation::Despawn)return e.package.empty() && e.actorClass.empty() &&
        e.proxyPackage.empty() && e.proxyClass.empty() && e.mapActor.empty();
    auto name=[](const std::string& s){
        return !s.empty() && s.size()<=48 && std::all_of(s.begin(),s.end(),[](unsigned char c){
            return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';
        });
    };
    return ((e.kind==EntityKind::Mover || e.kind==EntityKind::Objective) ? name(e.mapActor) && e.proxyPackage.empty() : (e.mapActor.empty()||(e.kind==EntityKind::Pickup&&name(e.mapActor)))) &&
        name(e.package) && name(e.actorClass) &&
        (e.proxyPackage.empty()==e.proxyClass.empty()) &&
        (e.proxyPackage.empty() || (name(e.proxyPackage) && name(e.proxyClass)));
}
template<class T> std::vector<T> take(std::deque<T>& q)
{ std::vector<T> v(q.begin(),q.end()); q.clear(); return v; }
}
bool validWorld(const World& world)
{
    if (world.size()>MaxEntities || std::count_if(world.begin(),world.end(),[](const Entity& e){return e.physical.kind!=0;})>48 || std::count_if(world.begin(),world.end(),[](const Entity& e){return e.combat!=NativeCombat{};})>64) return false;
    uint32_t last=0;
    for (const auto& e:world)
    { if (!validEntity(e) || e.ref.id<=last) return false; last=e.ref.id; }
    for (const auto& e:world)
    {
        for (auto r:{e.owner,e.weapon,e.shield,e.combat.offhand})
        {
            if (!r.id) continue;
            const auto p=find(world,r.id);
            if (!p || p->ref!=r || p->ref==e.ref) return false;
        }
        if (e.weapon.id)
        { auto k=find(world,e.weapon.id)->kind; if (k!=EntityKind::Weapon && k!=EntityKind::Limb) return false; }
        if (e.shield.id && find(world,e.shield.id)->kind!=EntityKind::Shield) return false;
        if(e.combat.offhand.id) {
            const auto* off=find(world,e.combat.offhand.id);
            if(!e.weapon.id || e.weapon==e.combat.offhand || off->kind!=EntityKind::Weapon ||
                off->owner!=e.ref || off->attachmentJoint==255 || (off->flags&32))return false;
        }
        auto p=&e;
        for (size_t depth=0; p->owner.id; ++depth)
        {
            // Owner cycles only. Pawn->weapon and weapon->owner is legitimate.
            if (depth>=world.size()) return false;
            p=find(world,p->owner.id);
            if (!p) return false;
        }
    }
    return true;
}
bool validMovementAck(const std::optional<MovementAck>& ack,const World& world)
{
    if(!ack)return true;
    const auto& a=*ack;auto* e=find(world,a.ref.id);
    return e && e->ref==a.ref && e->kind==EntityKind::Pawn && e->life==a.life &&
        a.input<UINT64_MAX && a.heldTicks<=255 && a.mode<=MovementFalling &&
        (a.mode==MovementUnsupported || (a.input && a.heldTicks && a.speed && a.step && a.friction<=25600 &&
            ((a.mode==MovementWalking && (e->flags&3)==3 && a.acceleration) ||
             (a.mode==MovementFalling && (e->flags&3)==1 && a.friction))));
}
bool encodeSnapshot(const Snapshot& s,const Snapshot* base,Bytes& out)
{
    if (!number(s.id) || !number(s.tick) || !validWorld(s.world) || !validMovementAck(s.movement,s.world) ||
        (base ? (!number(base->id) || !number(base->tick) || s.baseline!=base->id || base->id>=s.id || base->tick>s.tick || !validWorld(base->world)) : s.baseline!=0)) return false;
    World changed; std::vector<EntityRef> removed;
    for (auto& e:s.world)
    {
        auto old=base?find(base->world,e.ref.id):nullptr;
        if (old && (e.ref.generation<old->ref.generation ||
            (e.ref.generation==old->ref.generation && (e.kind!=old->kind || e.life<old->life)))) return false;
        if (!old || *old!=e) changed.push_back(e);
    }
    if (base) for (auto& e:base->world) if (!find(s.world,e.ref.id)) removed.push_back(e.ref);
    Writer w; w.put(uint16_t(7)); w.put(s.id); w.put(s.baseline); w.put(s.tick);
    w.put(uint16_t(changed.size())); w.put(uint16_t(removed.size()));
    w.put(uint8_t(s.movement.has_value()));
    if(s.movement) {
        const auto& a=*s.movement;w.ref(a.ref);w.put(a.life);w.put(a.input);
        w.put(a.heldTicks);w.put(a.mode);w.put(a.speed);w.put(a.acceleration);w.put(a.friction);w.put(a.step);
    }
    for (auto& e:changed) writeEntity(w,e);
    for (auto r:removed) w.ref(r);
    if (w.b.size()>MaxSnapshotBytes) return false;
    out=std::move(w.b); return true;
}
bool decodeSnapshot(std::span<const uint8_t> bytes,const Snapshot* base,Snapshot& out)
{
    if (bytes.size()<31 || bytes.size()>MaxSnapshotBytes) return false;
    Reader r{bytes}; if (r.get<uint16_t>()!=7) return false;
    Snapshot s; s.id=r.get<uint64_t>(); s.baseline=r.get<uint64_t>(); s.tick=r.get<uint64_t>();
    auto changed=r.get<uint16_t>(),removed=r.get<uint16_t>();
    auto hasMovement=r.get<uint8_t>();
    if(hasMovement>1)return false;
    if(hasMovement) {
        MovementAck a;a.ref=r.ref();a.life=r.get<uint32_t>();a.input=r.get<uint64_t>();
        a.heldTicks=r.get<uint16_t>();a.mode=r.get<uint8_t>();a.speed=r.get<uint16_t>();
        a.acceleration=r.get<uint16_t>();a.friction=r.get<uint16_t>();a.step=r.get<uint16_t>();s.movement=a;
    }
    if (changed>MaxEntities || removed>MaxEntities || bytes.size()<31+size_t(hasMovement)*31+size_t(changed)*77+size_t(removed)*8 ||
        !number(s.id) || !number(s.tick) || (base?(!number(base->id) || !number(base->tick) || base->id!=s.baseline || base->id>=s.id || base->tick>s.tick || !validWorld(base->world)):s.baseline!=0)) return false;
    std::map<uint32_t,Entity> entities;
    if (base) for (auto& e:base->world) entities.emplace(e.ref.id,e);
    uint32_t last=0; std::vector<uint32_t> updates;
    for (unsigned i=0;i<changed;++i)
    {
        auto e=readEntity(r);
        if (!validEntity(e) || e.ref.id<=last) return false;
        last=e.ref.id; updates.push_back(last);
        auto old=entities.find(last);
        if (old!=entities.end() && (e.ref.generation<old->second.ref.generation ||
            (e.ref.generation==old->second.ref.generation && (e.kind!=old->second.kind || e.life<old->second.life)) || e==old->second)) return false;
        entities[last]=e;
    }
    last=0;
    for (unsigned i=0;i<removed;++i)
    {
        auto ref=r.ref(); auto it=entities.find(ref.id);
        if (ref.id<=last || it==entities.end() || it->second.ref!=ref || std::binary_search(updates.begin(),updates.end(),ref.id)) return false;
        last=ref.id; entities.erase(it);
    }
    if (!r.done() || entities.size()>MaxEntities) return false;
    for (auto& [id,e]:entities) { (void)id; s.world.push_back(e); }
    if (!validWorld(s.world) || !validMovementAck(s.movement,s.world)) return false;
    out=std::move(s); return true;
}
bool encodeInputs(uint32_t epoch,const InputBatch& b,Bytes& out)
{
    if (!epoch || !validBatch(b)) return false;
    Writer w; w.header(1,epoch); w.put(uint8_t(b.inputs.size())); w.put(uint8_t(b.actions.size()));
    w.put(b.snapshots.installed); w.put(b.snapshots.receiving); w.put(b.snapshots.receivedMask); w.put(uint8_t(b.snapshots.requestFull));
    for (auto& i:b.inputs)
    { w.put(i.sequence); w.put(i.createdMs); w.put(i.life); w.put(i.forward); w.put(i.strafe); w.put(i.pitch); w.put(i.yaw); w.put(i.held); }
    for (auto& a:b.actions)
    { w.put(a.id); w.put(a.input); w.put(a.createdMs); w.put(a.life); w.put(uint8_t(a.kind)); }
    if (w.b.size()>MaxPayloadBytes) return false;
    out=std::move(w.b); return true;
}
bool decodeInputs(uint32_t epoch,std::span<const uint8_t> bytes,InputBatch& out)
{
    if (bytes.size()>MaxPayloadBytes) return false;
    Reader r{bytes}; if (!r.header(1,epoch)) return false;
    auto ni=r.get<uint8_t>(),na=r.get<uint8_t>(); if (ni>8 || na>4) return false;
    InputBatch b; b.snapshots.installed=r.get<uint64_t>(); b.snapshots.receiving=r.get<uint64_t>(); b.snapshots.receivedMask=r.get<uint64_t>();
    auto full=r.get<uint8_t>(); if (full>1) return false; b.snapshots.requestFull=full;
    for (unsigned n=0;n<ni;++n)
    {
        Input i; i.sequence=r.get<uint64_t>(); i.createdMs=r.get<uint64_t>(); i.life=r.get<uint32_t>();
        i.forward=r.get<int16_t>(); i.strafe=r.get<int16_t>(); i.pitch=r.get<int16_t>(); i.yaw=r.get<uint16_t>(); i.held=r.get<uint16_t>(); b.inputs.push_back(i);
    }
    for (unsigned n=0;n<na;++n)
    { Action a; a.id=r.get<uint64_t>(); a.input=r.get<uint64_t>(); a.createdMs=r.get<uint64_t>(); a.life=r.get<uint32_t>(); a.kind=ActionKind(r.get<uint8_t>()); b.actions.push_back(a); }
    if (!r.done() || !validBatch(b)) return false;
    out=std::move(b); return true;
}
bool encodeFeedback(uint32_t epoch,const InputFeedback& f,Bytes& out)
{
    if (!epoch || !validFeedback(f)) return false;
    Writer w; w.header(2,epoch); w.put(f.highestInput); w.put(uint8_t(f.actions.size()));
    for (auto a:f.actions) { w.put(a.id); w.put(uint8_t(a.status)); }
    out=std::move(w.b); return true;
}
bool decodeFeedback(uint32_t epoch,std::span<const uint8_t> bytes,InputFeedback& out)
{
    if (bytes.size()>MaxPayloadBytes) return false;
    Reader r{bytes}; if (!r.header(2,epoch)) return false;
    InputFeedback f; f.highestInput=r.get<uint64_t>(); auto n=r.get<uint8_t>(); if (n>64) return false;
    for (unsigned i=0;i<n;++i) { auto id=r.get<uint64_t>(); f.actions.push_back({id,Admission(r.get<uint8_t>())}); }
    if (!r.done() || !validFeedback(f)) return false;
    out=std::move(f); return true;
}
namespace {
bool validSound(const SoundCue& c) {
    return c.id && c.emitter<0x100000 && c.object && c.object<=1000000 &&
        !c.package.empty() && c.package.size()<=48 &&
        std::all_of(c.package.begin(),c.package.end(),[](unsigned char v){return (v>='a'&&v<='z')||(v>='A'&&v<='Z')||(v>='0'&&v<='9')||v=='_'||v=='-';}) &&
        ((!c.source.id&&!c.source.generation)||(c.source.id&&c.source.generation)) &&
        c.volume<=2048 && c.radius>0 && c.pitch>=32 && c.pitch<=1024 && c.channel<=15 &&
        std::all_of(c.position.begin(),c.position.end(),[](int32_t v){return v>=-134217728 && v<=134217728;});
}
}
bool encodeSoundFeedback(uint32_t epoch,const InputFeedback& f,const std::vector<SoundCue>& cues,Bytes& out) {
    Bytes feedback;if(cues.empty() || cues.size()>4 || !encodeFeedback(epoch,f,feedback))return false;
    Writer w;w.header(11,epoch);w.put(uint16_t(feedback.size()));w.b.insert(w.b.end(),feedback.begin(),feedback.end());w.put(uint8_t(cues.size()));
    uint64_t prior=0;
    for(const auto& c:cues) {
        if(!validSound(c) || c.id<=prior)return false;prior=c.id;
        w.put(c.id);w.ref(c.source);w.put(c.emitter);w.put(c.object);w.put(uint8_t(c.package.size()));
        w.b.insert(w.b.end(),c.package.begin(),c.package.end());for(auto v:c.position)w.put(v);
        w.put(c.volume);w.put(c.radius);w.put(c.pitch);w.put(c.channel);
    }
    if(w.b.size()>MaxPayloadBytes)return false;out=std::move(w.b);return true;
}
bool decodeSoundFeedback(uint32_t epoch,std::span<const uint8_t> bytes,InputFeedback& out,std::vector<SoundCue>& sounds) {
    if(bytes.size()>MaxPayloadBytes)return false;
    Reader r{bytes};if(!r.header(11,epoch))return false;
    auto size=r.get<uint16_t>();if(!r.ok || size>bytes.size()-r.at)return false;
    InputFeedback f;if(!decodeFeedback(epoch,bytes.subspan(r.at,size),f))return false;r.at+=size;
    auto count=r.get<uint8_t>();if(!count || count>4)return false;std::vector<SoundCue> cues;uint64_t prior=0;
    for(unsigned i=0;i<count;++i) {
        SoundCue c;c.id=r.get<uint64_t>();c.source=r.ref();c.emitter=r.get<uint32_t>();c.object=r.get<uint32_t>();
        auto n=r.get<uint8_t>();if(!r.ok || n>48 || n>bytes.size()-r.at)return false;
        c.package.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
        for(auto& v:c.position)v=r.get<int32_t>();c.volume=r.get<uint16_t>();c.radius=r.get<uint16_t>();c.pitch=r.get<uint16_t>();c.channel=r.get<uint8_t>();
        if(!r.ok || !validSound(c) || c.id<=prior)return false;prior=c.id;cues.push_back(std::move(c));
    }
    if(!r.done())return false;out=std::move(f);sounds=std::move(cues);return true;
}
bool encodeActionDecision(uint32_t epoch,const ActionResult& a,Bytes& out)
{
    if(!epoch || !number(a.id) || (a.status!=Admission::Started &&
        a.status!=Admission::ExecutionRejected && a.status!=Admission::Cancelled))return false;
    Writer w;w.header(7,epoch);w.put(a.id);w.put(uint8_t(a.status));out=std::move(w.b);return true;
}
bool decodeActionDecision(uint32_t epoch,std::span<const uint8_t> bytes,ActionResult& out)
{
    if(bytes.size()!=18)return false;
    Reader r{bytes};if(!r.header(7,epoch))return false;
    ActionResult a{r.get<uint64_t>(),Admission(r.get<uint8_t>())};
    if(!r.done() || !number(a.id) || (a.status!=Admission::Started &&
        a.status!=Admission::ExecutionRejected && a.status!=Admission::Cancelled))return false;
    out=a;return true;
}
bool encodeClose(uint32_t epoch,CloseReason reason,Bytes& out)
{
    if(!epoch || (reason!=CloseReason::Leave && reason!=CloseReason::MatchEnded))return false;
    Writer w;w.header(8,epoch);w.put(uint8_t(reason));out=std::move(w.b);return true;
}
bool decodeClose(uint32_t epoch,std::span<const uint8_t> bytes,CloseReason& out)
{
    if(bytes.size()!=10)return false;Reader r{bytes};if(!r.header(8,epoch))return false;
    auto reason=CloseReason(r.get<uint8_t>());
    if(!r.done() || (reason!=CloseReason::Leave && reason!=CloseReason::MatchEnded))return false;
    out=reason;return true;
}
bool encodePart(uint32_t epoch,const SnapshotPart& p,Bytes& out)
{
    if (!epoch || !validPart(p)) return false;
    Writer w; w.header(3,epoch); w.put(p.id); w.put(p.baseline); w.put(p.tick); w.put(p.total); w.put(p.index); w.put(p.count);
    w.b.insert(w.b.end(),p.bytes.begin(),p.bytes.end()); out=std::move(w.b); return true;
}
bool decodePart(uint32_t epoch,std::span<const uint8_t> bytes,SnapshotPart& out)
{
    if (bytes.size()<39 || bytes.size()>MaxPayloadBytes) return false;
    Reader r{bytes}; if (!r.header(3,epoch)) return false;
    SnapshotPart p; p.id=r.get<uint64_t>(); p.baseline=r.get<uint64_t>(); p.tick=r.get<uint64_t>(); p.total=r.get<uint32_t>(); p.index=r.get<uint8_t>(); p.count=r.get<uint8_t>();
    p.bytes.assign(bytes.begin()+r.at,bytes.end()); if (!r.ok || !validPart(p)) return false;
    out=std::move(p); return true;
}
bool encodeCombatOutcome(uint32_t epoch,const CombatOutcome& c,Bytes& out)
{
    if (!epoch || !validCombat(c)) return false;
    Writer w; w.header(4,epoch); w.put(c.id); w.put(c.tick); w.put(c.action);
    w.ref(c.attacker); w.ref(c.target); w.put(c.attackerLife); w.put(c.targetLife);
    w.put(uint8_t(c.move)); w.put(uint8_t(c.contact)); w.put(c.targetHealth); w.put(uint8_t(c.staggered));
    out=std::move(w.b); return true;
}
bool decodeCombatOutcome(uint32_t epoch,std::span<const uint8_t> bytes,CombatOutcome& out)
{
    if (bytes.size()!=62) return false;
    Reader r{bytes}; if (!r.header(4,epoch)) return false;
    CombatOutcome c; c.id=r.get<uint64_t>(); c.tick=r.get<uint64_t>(); c.action=r.get<uint64_t>();
    c.attacker=r.ref(); c.target=r.ref(); c.attackerLife=r.get<uint32_t>(); c.targetLife=r.get<uint32_t>();
    c.move=CombatMove(r.get<uint8_t>()); c.contact=CombatContact(r.get<uint8_t>());
    c.targetHealth=r.get<int16_t>(); auto stagger=r.get<uint8_t>();
    if (!r.done() || stagger>1) return false;
    c.staggered=stagger;
    if (!validCombat(c)) return false;
    out=c; return true;
}
bool encodeMatchContract(uint32_t epoch,const MatchContract& m,Bytes& out)
{
    if(!epoch || !validMatch(m))return false;
    Writer w;w.header(5,epoch);w.put(m.tickHz);w.put(m.maxPlayers);w.put(m.rulesRevision);
    w.put(uint8_t(m.map.size()));w.b.insert(w.b.end(),m.map.begin(),m.map.end());
    w.b.insert(w.b.end(),m.gameplayHash.begin(),m.gameplayHash.end());
    w.b.insert(w.b.end(),m.mapHash.begin(),m.mapHash.end());w.put(uint8_t(m.mode));w.put(m.teams);w.put(m.arenaTeamSize);w.put(m.limbsScore);
    out=std::move(w.b);return true;
}
bool decodeMatchContract(uint32_t epoch,std::span<const uint8_t> bytes,MatchContract& out)
{
    if(bytes.size()<9+2+2+4+1+1+68 || bytes.size()>9+2+2+4+1+48+68)return false;
    Reader r{bytes};if(!r.header(5,epoch))return false;
    MatchContract m;m.tickHz=r.get<uint16_t>();m.maxPlayers=r.get<uint16_t>();m.rulesRevision=r.get<uint32_t>();
    auto n=r.get<uint8_t>();if(!r.ok || !n || n>48 || bytes.size()!=9+2+2+4+1+n+68)return false;
    m.map.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
    std::copy_n(bytes.data()+r.at,32,m.gameplayHash.begin());r.at+=32;
    std::copy_n(bytes.data()+r.at,32,m.mapHash.begin());r.at+=32;
    m.mode=GameMode(r.get<uint8_t>());m.teams=r.get<uint8_t>();m.arenaTeamSize=r.get<uint8_t>();m.limbsScore=r.get<uint8_t>();
    if(!r.done() || !validMatch(m))return false;
    out=std::move(m);return true;
}
namespace {
bool validStatus(const MatchStatus& s) {
    if(s.phase<MatchPhase::Waiting || s.phase>MatchPhase::Intermission || s.secondsLeft>86400 ||
       s.fragLimit>10000 || s.teams<2 || s.teams>4 || s.scores.size()>32 || s.winner>32 ||
       (s.phase!=MatchPhase::Intermission && s.winner))return false;
    if(s.mode!=GameMode::Deathmatch && s.mode!=GameMode::TeamDeathmatch && s.mode!=GameMode::Arena && s.mode!=GameMode::HeadBall)return false;
    if(s.headBallCountdown>120 || std::any_of(s.readyTeams.begin(),s.readyTeams.end(),[](uint8_t r){return r>1;}) ||
       (s.headBallStarted && s.headBallCountdown) ||
       (s.mode!=GameMode::HeadBall && (s.headBallStarted || s.headBallCountdown || s.readyTeams!=std::array<uint8_t,4>{})))return false;
    if(s.arenaPhase>3 || s.arenaCountdown>120 || s.arenaRound>1000000 ||
       (s.mode!=GameMode::Arena && (s.arenaPhase || s.arenaCountdown || s.arenaRound)))return false;
    if(!teamMode(s.mode) && (s.winningTeam!=255 || s.teamScores!=std::array<int32_t,4>{}))return false;
    if(teamMode(s.mode) && (s.winner || (s.winningTeam>=s.teams && s.winningTeam!=255) ||
       (s.phase!=MatchPhase::Intermission && s.winningTeam!=255)))return false;
    for(size_t t=0;t<s.teamScores.size();++t)if(s.teamScores[t]<-1000000 || s.teamScores[t]>1000000 || (t>=s.teams && (s.teamScores[t] || s.readyTeams[t])))return false;
    uint32_t last=0;bool found=s.winner==0;
    for(const auto& p:s.scores) {
        if(!validRef(p.pawn) || (s.mode==GameMode::Deathmatch?p.team!=255:teamMode(s.mode)?p.team>=s.teams:(p.team>1 && p.team!=255)) || p.player<=last || p.player>32 || p.frags<-1000000 || p.frags>1000000)return false;
        if((p.queue!=255 && (p.queue<1 || p.queue>16)) || (s.mode!=GameMode::Arena && p.queue!=255))return false;
        last=p.player;found|=p.player==s.winner;
    }
    return found;
}
}
bool encodeMatchStatus(uint32_t epoch,const MatchStatus& s,Bytes& out) {
    if(!epoch || !validStatus(s))return false;
    Writer w;w.header(9,epoch);w.put(uint8_t(s.phase));w.put(s.secondsLeft);w.put(s.fragLimit);w.put(s.winner);
    w.put(uint8_t(s.scores.size()));
    for(const auto& p:s.scores){w.put(p.player);w.put(p.frags);w.put(p.deaths);w.put(p.team);w.put(p.queue);w.ref(p.pawn);}
    w.put(uint8_t(s.mode));w.put(s.winningTeam);for(auto score:s.teamScores)w.put(score);w.put(s.teams);
    w.put(s.arenaPhase);w.put(s.arenaCountdown);w.put(s.arenaRound);
    w.put(uint8_t(s.headBallStarted));w.put(s.headBallCountdown);for(auto ready:s.readyTeams)w.put(ready);
    out=std::move(w.b);return true;
}
bool decodeMatchStatus(uint32_t epoch,std::span<const uint8_t> bytes,MatchStatus& out) {
    if(bytes.size()>694)return false;
    Reader r{bytes};if(!r.header(9,epoch))return false;
    MatchStatus s;s.phase=MatchPhase(r.get<uint8_t>());s.secondsLeft=r.get<uint32_t>();
    s.fragLimit=r.get<uint32_t>();s.winner=r.get<uint32_t>();auto count=r.get<uint8_t>();
    if(count>32)return false;
    for(unsigned i=0;i<count;++i){PlayerScore p;p.player=r.get<uint32_t>();p.frags=r.get<int32_t>();p.deaths=r.get<uint16_t>();p.team=r.get<uint8_t>();p.queue=r.get<uint8_t>();p.pawn=r.ref();s.scores.push_back(p);}
    s.mode=GameMode(r.get<uint8_t>());s.winningTeam=r.get<uint8_t>();for(auto& score:s.teamScores)score=r.get<int32_t>();s.teams=r.get<uint8_t>();
    s.arenaPhase=r.get<uint8_t>();s.arenaCountdown=r.get<uint8_t>();s.arenaRound=r.get<uint32_t>();
    auto started=r.get<uint8_t>();if(started>1)return false;s.headBallStarted=started!=0;
    s.headBallCountdown=r.get<uint8_t>();for(auto& ready:s.readyTeams)ready=r.get<uint8_t>();
    if(!r.done() || !validStatus(s))return false;
    out=std::move(s);return true;
}
bool encodeMapTravel(uint32_t sessionEpoch,const MapTravel& t,Bytes& out) {
    Bytes contract;if(!sessionEpoch || !t.epoch || !encodeMatchContract(t.epoch,t.match,contract))return false;
    Writer w;w.header(10,sessionEpoch);w.put(t.epoch);w.b.insert(w.b.end(),contract.begin(),contract.end());
    out=std::move(w.b);return true;
}
bool decodeMapTravel(uint32_t sessionEpoch,std::span<const uint8_t> bytes,MapTravel& out) {
    Reader r{bytes};if(!r.header(10,sessionEpoch))return false;
    MapTravel t;t.epoch=r.get<uint32_t>();
    if(!r.ok || !decodeMatchContract(t.epoch,bytes.subspan(r.at),t.match))return false;
    out=std::move(t);return true;
}
bool encodeActorEvent(uint32_t epoch,const ActorEvent& e,Bytes& out)
{
    if(!epoch || !validActorEvent(e))return false;
    Writer w;w.header(6,epoch);w.put(e.id);w.ref(e.ref);w.put(uint8_t(e.operation));w.put(uint8_t(e.kind));
    if(e.operation==ActorOperation::Appearance) {
        const auto& a=e.appearance;w.put(a.mesh);w.put(a.firstGroup);
        w.put(uint8_t(a.skeleton.package.size()));w.b.insert(w.b.end(),a.skeleton.package.begin(),a.skeleton.package.end());w.put(a.skeleton.object);w.put(a.scale);w.put(a.drawType);
        w.put(uint8_t(a.substitute.package.size()));w.b.insert(w.b.end(),a.substitute.package.begin(),a.substitute.package.end());w.put(a.substitute.object);
        for(auto f:a.groupFlags)w.put(f);for(auto f:a.jointFlags)w.put(f);
        for(const auto& skin:a.skins){w.put(uint8_t(skin.package.size()));w.b.insert(w.b.end(),skin.package.begin(),skin.package.end());w.put(skin.object);}
        if(w.b.size()>MaxPayloadBytes)return false;
        out=std::move(w.b);return true;
    }
    w.put(uint8_t(e.package.size()));w.b.insert(w.b.end(),e.package.begin(),e.package.end());
    w.put(uint8_t(e.actorClass.size()));w.b.insert(w.b.end(),e.actorClass.begin(),e.actorClass.end());
    w.put(uint8_t(e.proxyPackage.size()));w.b.insert(w.b.end(),e.proxyPackage.begin(),e.proxyPackage.end());
    w.put(uint8_t(e.proxyClass.size()));w.b.insert(w.b.end(),e.proxyClass.begin(),e.proxyClass.end());
    w.put(uint8_t(e.mapActor.size()));w.b.insert(w.b.end(),e.mapActor.begin(),e.mapActor.end());
    out=std::move(w.b);return true;
}
bool decodeActorEvent(uint32_t epoch,std::span<const uint8_t> bytes,ActorEvent& out)
{
    if(bytes.size()<32 || bytes.size()>MaxPayloadBytes)return false;
    Reader r{bytes};if(!r.header(6,epoch))return false;
    ActorEvent e;e.id=r.get<uint64_t>();e.ref=r.ref();e.operation=ActorOperation(r.get<uint8_t>());e.kind=EntityKind(r.get<uint8_t>());
    if(e.operation==ActorOperation::Appearance) {
        auto& a=e.appearance;a.mesh=r.get<uint8_t>();a.firstGroup=r.get<uint8_t>();
        auto len=r.get<uint8_t>();if(!r.ok || len>48 || bytes.size()-r.at<size_t(len)+7)return false;
        a.skeleton.package.assign(reinterpret_cast<const char*>(bytes.data()+r.at),len);r.at+=len;a.skeleton.object=r.get<uint32_t>();a.scale=r.get<int16_t>();a.drawType=r.get<uint8_t>();
        len=r.get<uint8_t>();if(!r.ok || len>48 || bytes.size()-r.at<size_t(len)+4)return false;
        a.substitute.package.assign(reinterpret_cast<const char*>(bytes.data()+r.at),len);r.at+=len;a.substitute.object=r.get<uint32_t>();
        for(auto& f:a.groupFlags)f=r.get<int32_t>();for(auto& f:a.jointFlags)f=r.get<uint8_t>();
        for(auto& skin:a.skins) {
            auto count=r.get<uint8_t>();if(!r.ok || count>48 || bytes.size()-r.at<size_t(count)+4)return false;
            skin.package.assign(reinterpret_cast<const char*>(bytes.data()+r.at),count);r.at+=count;skin.object=r.get<uint32_t>();
        }
        if(!r.done() || !validActorEvent(e))return false;out=std::move(e);return true;
    }
    auto n=r.get<uint8_t>();if(!r.ok || n>48 || bytes.size()-r.at<size_t(n)+1)return false;
    e.package.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
    n=r.get<uint8_t>();if(!r.ok || n>48 || bytes.size()-r.at<size_t(n)+2)return false;
    e.actorClass.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
    n=r.get<uint8_t>();if(!r.ok || n>48 || bytes.size()-r.at<size_t(n)+1)return false;
    e.proxyPackage.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
    n=r.get<uint8_t>();if(!r.ok || n>48 || bytes.size()-r.at<size_t(n)+1)return false;
    e.proxyClass.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
    n=r.get<uint8_t>();if(!r.ok || n>48 || bytes.size()-r.at!=n)return false;
    e.mapActor.assign(reinterpret_cast<const char*>(bytes.data()+r.at),n);r.at+=n;
    if(!r.done() || !validActorEvent(e))return false;
    out=std::move(e);return true;
}

std::optional<uint64_t> InputSender::capture(Input input,uint64_t now)
{
    if (now<lastNow || now>ClockSync::MaxTime || nextInput==UINT64_MAX) return {};
    input.sequence=nextInput; input.createdMs=now;
    if (!validInput(input)) return {};
    lastNow=now; ++nextInput; history.push_back(input);
    while (history.size()>32) history.pop_front();
    return input.sequence;
}
std::optional<uint64_t> InputSender::act(ActionKind kind,uint64_t now)
{
    if (now<lastNow || now>ClockSync::MaxTime || history.empty() || nextAction==UINT64_MAX || actions.size()>=32 || now-history.back().createdMs>InputMaxAge) return {};
    Action a{nextAction,history.back().sequence,now,history.back().life,kind};
    if (!validAction(a)) return {};
    lastNow=now; ++nextAction; actions.emplace(a.id,Pending{a,history.back()}); return a.id;
}
InputBatch InputSender::batch(uint64_t now,SnapshotFeedback snapshots)
{
    InputBatch b; b.snapshots=snapshots;
    if (now<lastNow || now>ClockSync::MaxTime) return b;
    lastNow=now;
    std::map<uint64_t,Input> selected;
    for (auto it=actions.begin();it!=actions.end();)
    {
        const auto& p=it->second;
        if (now-p.action.createdMs>=1000)
        {
            unconfirmed.push_back(it->first); if (unconfirmed.size()>64) unconfirmed.pop_front();
            retired[it->first]=true; while (retired.size()>64) retired.erase(retired.begin());
            it=actions.erase(it); continue;
        }
        if (b.actions.size()<4 && now-p.action.createdMs<=InputMaxAge && now-p.context.createdMs<=InputMaxAge)
        { b.actions.push_back(p.action); selected.emplace(p.context.sequence,p.context); }
        ++it;
    }
    for (auto it=history.rbegin();it!=history.rend() && selected.size()<8;++it)
        if (now-it->createdMs<=InputMaxAge) selected.emplace(it->sequence,*it);
    for (auto& [id,input]:selected) { (void)id; b.inputs.push_back(input); }
    return b;
}
void InputSender::receive(const InputFeedback& f)
{
    if (!validFeedback(f) || f.highestInput>=nextInput) return;
    for (auto result:f.actions)
    {
        bool known=bool(actions.erase(result.id)) || bool(retired.erase(result.id)) || reported.contains(result.id);
        if (!known) continue;
        auto previous=reported.find(result.id);
        if (previous!=reported.end() && (previous->second==result.status ||
            (previous->second!=Admission::Admitted && previous->second!=Admission::Started))) continue;
        if (previous!=reported.end() && previous->second==Admission::Started && result.status==Admission::Admitted) continue;
        reported[result.id]=result.status;
        while (reported.size()>256) reported.erase(reported.begin());
        results.push_back(result); if (results.size()>64) results.pop_front();
    }
}
void InputSender::receiveDecision(ActionResult decision)
{
    if(decision.status==Admission::Started || decision.status==Admission::ExecutionRejected ||
        decision.status==Admission::Cancelled)receive(InputFeedback{0,{decision}});
}
std::vector<ActionResult> InputSender::drainResults() { return take(results); }
std::vector<uint64_t> InputSender::drainUnconfirmed() { return take(unconfirmed); }
InputReceiver::InputReceiver(uint32_t l):life(l) { if (!l) throw std::invalid_argument("life must be nonzero"); }
void InputReceiver::setLife(uint32_t l)
{
    if (!l || l<life) throw std::invalid_argument("life must not regress or wrap");
    if (l!=life) {
        for (const auto& a:admitted) remember({a.action.id,Admission::WrongLife});
        life=l; current.reset(); admitted.clear(); observed.clear();
    }
}
void InputReceiver::remember(ActionResult r)
{
    decisions[r.id]=r;
    while (!decisions.empty() && highestAction-decisions.begin()->first>=256) decisions.erase(decisions.begin());
    std::erase_if(recent,[&](auto old){return old.id==r.id;}); recent.push_back(r);
    while (recent.size()>64) recent.pop_front();
}
bool InputReceiver::receive(const InputBatch& b,uint64_t now,const MapTime& mapTime)
{
    if (!validBatch(b) || now<lastNow || now>ClockSync::MaxTime) return false;
    // Reject implausible sequence jumps atomically before changing admission state.
    for (auto& i:b.inputs) if (i.sequence>highestInput && i.sequence-highestInput>4096) return false;
    for (auto& a:b.actions) if (a.id>highestAction && a.id-highestAction>256) return false;
    for (auto& i:b.inputs)
    {
        auto old=observed.find(i.sequence);
        if (old!=observed.end() && old->second!=i) return false;
    }
    lastNow=now;
    std::erase_if(admitted,[&](const auto& a){
        if (now-a.localTime<=InputMaxAge) return false;
        remember({a.action.id,Admission::Expired}); return true;
    });
    auto elapsed=(now-refillAt)/50;
    if (elapsed) { tokens=unsigned(std::min(uint64_t(8),uint64_t(tokens)+elapsed)); refillAt=now; }
    std::map<uint64_t,Input> mapped;
    for (auto input:b.inputs)
    {
        auto time=mapTime(input.createdMs);
        if (!time || *time>now || now-*time>InputMaxAge || input.life!=life) continue;
        if (input.sequence<=highestInput && !observed.contains(input.sequence)) continue;
        if (input.sequence>highestInput && current && *time<current->createdMs) continue;
        observed.emplace(input.sequence,input);
        while (observed.size()>256) observed.erase(observed.begin());
        input.createdMs=*time; mapped.emplace(input.sequence,input);
        if (input.sequence>highestInput) { highestInput=input.sequence; current=input; }
    }
    for (auto a:b.actions)
    {
        auto known=decisions.find(a.id);
        if (known!=decisions.end()) { remember(known->second); continue; }
        if (a.id<=highestAction && highestAction-a.id>=256)
        { // Do not put obsolete IDs back into the dedupe map.
            std::erase_if(recent,[&](auto r){return r.id==a.id;}); recent.push_back({a.id,Admission::TooOld});
            if (recent.size()>64) recent.pop_front(); continue;
        }
        highestAction=std::max(highestAction,a.id);
        auto time=mapTime(a.createdMs); auto input=mapped.find(a.input);
        Admission status=Admission::Admitted;
        if (a.life!=life) status=Admission::WrongLife;
        else if (!time || *time>now || now-*time>InputMaxAge) status=Admission::Expired;
        else if (input==mapped.end() || *time<input->second.createdMs) status=Admission::MissingInput;
        else if (!tokens) status=Admission::RateLimited;
        else if (admitted.size()>=64) status=Admission::Busy;
        if (status==Admission::Admitted) { --tokens; admitted.push_back({a,input->second,*time}); }
        remember({a.id,status});
    }
    return true;
}
Input InputReceiver::held(uint64_t now) const
{
    if (current && now>=lastNow && now>=current->createdMs && now-current->createdMs<=InputMaxAge) return *current;
    Input neutral; neutral.life=life; return neutral;
}
InputFeedback InputReceiver::feedback() const
{
    InputFeedback f; f.highestInput=highestInput; f.actions.assign(recent.begin(),recent.end());
    std::sort(f.actions.begin(),f.actions.end(),[](auto a,auto b){return a.id<b.id;}); return f;
}
std::vector<AdmittedAction> InputReceiver::drainActions(uint64_t now)
{
    if (now<lastNow || now>ClockSync::MaxTime) return {};
    lastNow=now;
    std::erase_if(admitted,[&](const auto& a){
        if (now-a.localTime<=InputMaxAge) return false;
        remember({a.action.id,Admission::Expired}); return true;
    });
    auto v=std::move(admitted); admitted.clear();
    std::sort(v.begin(),v.end(),[](const auto& a,const auto& b){return a.action.id<b.action.id;});
    return v;
}
bool InputReceiver::resolve(uint64_t id,Admission result)
{
    if (result!=Admission::Started && result!=Admission::ExecutionRejected && result!=Admission::Cancelled) return false;
    auto it=decisions.find(id);
    if (it==decisions.end() || (it->second.status!=Admission::Admitted &&
        !(it->second.status==Admission::Started && result==Admission::Cancelled))) return false;
    remember({id,result}); return true;
}

bool SnapshotSender::capture(World w,uint64_t tick,std::optional<MovementAck> movement)
{
    if (!number(tick) || tick<desiredTick || !validWorld(w)) return false;
    for (const auto& e:w)
    {
        auto old=find(desired,e.ref.id);
        if (old && (e.ref.generation<old->ref.generation || (e.ref.generation==old->ref.generation && e.kind!=old->kind))) return false;
    }
    // Coalescing may remove an entity before an older in-flight world is
    // installed. Validate against retained generations too, or a later delta
    // could become impossible to encode after capture already reported success.
    for (const auto& [id,snapshot]:cache)
    {
        (void)id;
        for (const auto& e:w)
        {
            auto old=find(snapshot.world,e.ref.id);
            if (old && (e.ref.generation<old->ref.generation || (e.ref.generation==old->ref.generation && e.kind!=old->kind))) return false;
        }
    }
    if(!validMovementAck(movement,w))return false;
    desired=std::move(w); desiredMovement=movement; desiredTick=tick; dirty=true;++counters.captures; return true;
}
bool SnapshotSender::receive(const SnapshotFeedback& f)
{
    if ((!f.receiving && f.receivedMask) || f.installed==UINT64_MAX || f.receiving==UINT64_MAX) return false;
    if (active && f.receiving==active->id &&
        (f.receivedMask & ~maskFor((encoded.size()+SnapshotPartBytes-1)/SnapshotPartBytes))) return false;
    if (f.installed>installed)
    {
        auto it=cache.find(f.installed); if (it==cache.end()) return false;
        installed=f.installed;
        if (active && installed==active->id) { active.reset(); encoded.clear(); }
    }
    if(active && f.receiving==active->id && !f.receivedMask) {
        active.reset();encoded.clear();dirty=true;
    }
    if (f.requestFull)
    {
        // Invalidate an exact rejected transfer once. Plain repeated requests
        // must not continually cancel a healthy full bootstrap.
        if (active && active->baseline) { active.reset(); encoded.clear(); dirty=true; }
        if (!active) { dirty=true; full=true; }
    }
    if (active && f.receiving==active->id)
    {
        auto count=(encoded.size()+SnapshotPartBytes-1)/SnapshotPartBytes;
        if (f.receivedMask & ~maskFor(count)) return false;
        receivedMask|=f.receivedMask;
    }
    return true;
}
std::optional<SnapshotPart> SnapshotSender::poll(uint64_t now,bool transmissionQueueEmpty,uint64_t retryMs)
{
    if (now<lastNow || now>ClockSync::MaxTime) return {};
    lastNow=now;
    if (active && now-started>=SnapshotTransferTimeout) { active.reset(); encoded.clear(); dirty=true; full=true; }
    // Bounded gameplay deltas must not wait a whole RTT for installation ACKs.
    // Replace only after every fragment was offered once and at most 30 Hz.
    // Large transfers keep their selective retry path to avoid starvation.
    // Bound the flight window so an outage cannot evict an ACK's base.
    if(active && transmissionQueueEmpty && dirty && installed && active->baseline && active->id-installed<16 &&
        encoded.size()<=16*SnapshotPartBytes && now-started>=33) {
        auto count=(encoded.size()+SnapshotPartBytes-1)/SnapshotPartBytes;
        bool offered=true;for(size_t i=0;i<count;++i)offered &= nextSend[i]!=0;
        if(offered){active.reset();encoded.clear();++counters.replacements;}
    }
    if (!active)
    {
        if (!dirty || nextId==UINT64_MAX) return {};
        const Snapshot* base=nullptr;
        auto it=cache.find(installed); if (!full && it!=cache.end()) base=&it->second;
        Snapshot s{nextId,base?base->id:0,desiredTick,desired,desiredMovement};
        if (!encodeSnapshot(s,base,encoded)) return {};
        if(compress)encoded=packSnapshot(encoded);
        ++counters.transfers;counters.encodedBytes+=encoded.size();counters.latestBytes=encoded.size();
        counters.maximumBytes=std::max(counters.maximumBytes,encoded.size());
        ++nextId; active=s; cache.emplace(s.id,std::move(s));
        while (cache.size()>32) {
            auto oldest=cache.begin();if(oldest->first==installed)++oldest;
            cache.erase(oldest);
        }
        dirty=false; full=false; started=now; receivedMask=0; nextSend.fill(0); cursor=0;
    }
    size_t count=(encoded.size()+SnapshotPartBytes-1)/SnapshotPartBytes;
    for (size_t n=0;n<count;++n)
    {
        size_t index=(cursor+n)%count;
        if ((receivedMask & (uint64_t(1)<<index)) || now<nextSend[index]) continue;
        ++counters.parts;if(nextSend[index])++counters.retries;
        nextSend[index]=now+std::clamp(retryMs,uint64_t(100),uint64_t(2000)); cursor=(index+1)%count;
        size_t offset=index*SnapshotPartBytes, end=std::min(offset+SnapshotPartBytes,encoded.size());
        return SnapshotPart{active->id,active->baseline,active->tick,uint32_t(encoded.size()),uint8_t(index),uint8_t(count),Bytes(encoded.begin()+offset,encoded.begin()+end)};
    }
    return {};
}
bool SnapshotReceiver::receive(const SnapshotPart& p,uint64_t now)
{
    if (!validPart(p) || now<lastNow || now>ClockSync::MaxTime || p.id<=latest || p.id<=rejectedTransfer) return false;
    lastNow=now;
    if (pending && p.id!=pending->id) return false;
    if (p.baseline && !cache.contains(p.baseline)) { needFull=true; return false; }
    expireAssemblies(now);
    auto found=assemblies.find(p.id);
    if(found==assemblies.end()) {
        if(assemblies.size()==MaxAssemblies) {
            // A delayed old fragment cannot evict newer useful transfers.
            if(p.id<assemblies.begin()->first)return false;
            assemblies.erase(assemblies.begin());
        }
        found=assemblies.emplace(p.id,Assembly{p,now,0,Bytes(p.total)}).first;
        found->second.meta.bytes.clear();
    }
    auto& a=found->second;
    if (a.meta.baseline!=p.baseline || a.meta.tick!=p.tick || a.meta.total!=p.total || a.meta.count!=p.count)
    { resync(); return false; }
    auto offset=size_t(p.index)*SnapshotPartBytes;
    if ((a.mask & (uint64_t(1)<<p.index)) && !std::equal(p.bytes.begin(),p.bytes.end(),a.bytes.begin()+offset))
    { resync(); return false; }
    if (pending) return true; // Identical duplicate cannot replace an already validated candidate.
    std::copy(p.bytes.begin(),p.bytes.end(),a.bytes.begin()+offset); a.mask|=uint64_t(1)<<p.index;
    if (a.mask==maskFor(p.count))
    {
        Snapshot s; auto base=cache.find(p.baseline);Bytes expanded;
        if (!unpackSnapshot(a.bytes,expanded) || !decodeSnapshot(expanded,base==cache.end()?nullptr:&base->second,s) || s.id!=p.id || s.baseline!=p.baseline || s.tick!=p.tick ||
            (installed() && s.tick<installed()->tick)) { resync(); return false; }
        if (installed()) for (const auto& e:s.world)
        {
            auto old=find(installed()->world,e.ref.id);
            if (old && (e.ref.generation<old->ref.generation || (e.ref.generation==old->ref.generation && e.kind!=old->kind))) { resync(); return false; }
        }
        pending=std::move(s);
    }
    return true;
}
bool SnapshotReceiver::commit(uint64_t id)
{
    if (!pending || pending->id!=id || id<=latest) return false;
    cache.emplace(id,std::move(*pending)); pending.reset(); latest=id; needFull=false; rejectedTransfer=0;
    std::erase_if(assemblies,[&](const auto& entry){return entry.first<=id;});
    while (cache.size()>32) cache.erase(cache.begin());
    return true;
}
const Snapshot* SnapshotReceiver::installed() const
{ auto it=cache.find(latest); return it==cache.end()?nullptr:&it->second; }
void SnapshotReceiver::expireAssemblies(uint64_t now)
{
    const auto expired=std::erase_if(assemblies,[&](const auto& entry){return now-entry.second.at>=SnapshotTransferTimeout;});
    if(expired && assemblies.empty() && !pending)needFull=true;
}
SnapshotFeedback SnapshotReceiver::feedback(uint64_t now)
{
    if (now>=lastNow && now<=ClockSync::MaxTime)
    {
        lastNow=now;expireAssemblies(now);
    }
    const Assembly* assembly=nullptr;
    if(pending) {
        auto it=assemblies.find(pending->id);if(it!=assemblies.end())assembly=&it->second;
    } else if(!assemblies.empty())assembly=&assemblies.rbegin()->second;
    return {latest,assembly?assembly->meta.id:rejectedTransfer,assembly?assembly->mask:0,needFull};
}
void SnapshotReceiver::discardObsolete() {
    resync();needFull=installed()==nullptr;
}
void SnapshotReceiver::resync() {
    if(!assemblies.empty())rejectedTransfer=std::max(rejectedTransfer,assemblies.rbegin()->first);
    if(pending)rejectedTransfer=std::max(rejectedTransfer,pending->id);
    assemblies.clear();pending.reset();needFull=true;
}
bool encodeCharacterChoice(uint32_t epoch,CharacterChoice c,Bytes& out) {
    if(!epoch || !validCharacter(c))return false;
    Writer w;w.header(12,epoch);w.put(c.character);w.put(c.skin);out=std::move(w.b);return true;
}
bool decodeCharacterChoice(uint32_t epoch,std::span<const uint8_t> bytes,CharacterChoice& out) {
    if(bytes.size()!=11 || !epoch)return false;
    Reader r{bytes};
    if(r.get<uint32_t>()!=0x4f525231 || r.get<uint8_t>()!=12 || r.get<uint32_t>()!=epoch)return false;
    CharacterChoice c{r.get<uint8_t>(),r.get<uint8_t>()};
    if(!r.ok || !validCharacter(c))return false;
    out=c;return true;
}
ReplicationPeer::ReplicationPeer(SecureSession& s,MatchContract contract,CharacterChoice choice):session(s),match(std::move(contract)),replicationEpoch(s.worldEpoch()),localCharacter(choice)
{
    if(!validCharacter(choice) || !validMatch(match))throw std::invalid_argument("invalid match contract");
}
void ReplicationPeer::resetMap(MatchContract contract,uint32_t epoch) {
    match=std::move(contract);replicationEpoch=epoch;matchState=MatchState::Awaiting;matchSent=false;characterSent=false;receivedCharacter.reset();
    inputs={};commands=InputReceiver{};snapshots=SnapshotSender{};world={};
    lastActionOffered=nextFeedback=lastActive=lastCombatSent=lastCombatReceived=lastCombatTick=lastActorSent=lastActorReceived=0;
    combatResults.clear();actorEvents.clear();pendingDecisions.clear();
    pendingSounds.clear();receivedSounds.clear();nextSoundId=1;lastSoundReceived=0;nextSoundFeedback=0;
    combatOverflow=actorOverflow=false;receivedStatus.reset();travelRequest.reset();
    session.discardSnapshotParts();
}
bool ReplicationPeer::travel(const MatchContract& contract) {
    if(!session.isServer() || !matchAgreed() || closing || receivedClose || replicationEpoch==UINT32_MAX || !validMatch(contract))return false;
    Bytes b;if(!encodeMapTravel(session.worldEpoch(),{replicationEpoch+1,contract},b) || !session.enqueue(Lane::Control,std::move(b),0))return false;
    resetMap(contract,replicationEpoch+1);matchSent=true;return true;
}
bool ReplicationPeer::acceptTravel(const MatchContract& verified) {
    if(session.isServer() || !travelRequest || closing || receivedClose)return false;
    if(verified!=travelRequest->match){matchState=MatchState::Mismatch;return false;}
    auto epoch=travelRequest->epoch;resetMap(verified,epoch);matchState=MatchState::Agreed;return true;
}
bool ReplicationPeer::publishStatus(const MatchStatus& state) {
    if(!session.isServer() || !matchAgreed() || closing || receivedClose)return false;
    Bytes b;return encodeMatchStatus(replicationEpoch,state,b) && session.enqueue(Lane::Control,std::move(b),0);
}
void ReplicationPeer::publishSound(SoundCue c) {
    if(!session.isServer() || !matchAgreed() || closing || receivedClose || nextSoundId==UINT64_MAX)return;
    c.id=nextSoundId;if(!validSound(c))return;++nextSoundId;
    if(pendingSounds.size()>=32)pendingSounds.pop_front();
    pendingSounds.push_back({std::move(c),session.localTime(),0});
}
std::vector<SoundCue> ReplicationPeer::drainSounds() { return take(receivedSounds); }
bool ReplicationPeer::publishCombat(const CombatOutcome& c)
{
    if (closing || receivedClose || !session.isServer() || !session.ready() || !matchAgreed() || !validCombat(c) || c.id!=lastCombatSent+1 ||
        c.tick<lastCombatTick) return false;
    Bytes bytes;
    if (!encodeCombatOutcome(replicationEpoch,c,bytes) ||
        !session.enqueue(Lane::Control,std::move(bytes),0)) return false;
    lastCombatSent=c.id; lastCombatTick=c.tick; return true;
}
bool ReplicationPeer::publishActor(const ActorEvent& e)
{
    if(closing || receivedClose || !session.isServer() || !session.ready() || !matchAgreed() ||
       !validActorEvent(e) || e.id!=lastActorSent+1)return false;
    Bytes bytes;
    if(!encodeActorEvent(replicationEpoch,e,bytes) ||
       !session.enqueue(Lane::Control,std::move(bytes),0))return false;
    lastActorSent=e.id;return true;
}
bool ReplicationPeer::decideAction(uint64_t id,Admission decision)
{
    if(closing || receivedClose || !session.isServer() || !matchAgreed() || pendingDecisions.size()>=128 ||
        !commands.resolve(id,decision))return false;
    pendingDecisions.push_back({id,decision});return true;
}
std::vector<ActorEvent> ReplicationPeer::drainActors() { return take(actorEvents); }
std::vector<CombatOutcome> ReplicationPeer::drainCombat() { return take(combatResults); }
void ReplicationPeer::step(bool sendInputs)
{
    if (!session.ready()) return;
    const auto now=session.localTime();
    const auto epoch=replicationEpoch;
    if(matchState==MatchState::Mismatch && !closing)return;
    // Ordered and authenticated; the server never accepts arbitrary class paths.
    if(!session.isServer() && !characterSent && !travelRequest) {
        Bytes choice;
        if(encodeCharacterChoice(epoch,localCharacter,choice) && session.enqueue(Lane::Control,std::move(choice),0))characterSent=true;
        else return;
    }
    if(!matchSent && !travelRequest)
    {
        Bytes offer;
        if(encodeMatchContract(epoch,match,offer) && session.enqueue(Lane::Control,std::move(offer),0))matchSent=true;
    }
    for (auto& m:session.drain())
    {
        bool ok=false;
        if(m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==8) {
            CloseReason reason;
            if(decodeClose(session.worldEpoch(),m.payload,reason))receivedClose=reason;else ++rejected;
            continue;
        }
        if(!session.isServer() && m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==10) {
            MapTravel next;
            if(!travelRequest && replicationEpoch<UINT32_MAX && decodeMapTravel(session.worldEpoch(),m.payload,next) && next.epoch==replicationEpoch+1) {
                travelRequest=std::move(next);matchState=MatchState::Awaiting;
            } else ++rejected;
            continue;
        }
        // Cross-lane delayed records from the previous map must not poison the
        // new contract, actor sequence, snapshot baseline or action stream.
        if(travelRequest || m.payload.size()<9 || Reader{std::span<const uint8_t>(m.payload).subspan(5)}.get<uint32_t>()!=epoch) {++rejected;continue;}
        if(session.isServer() && m.lane==Lane::Control && m.payload[4]==12) {
            CharacterChoice choice;
            ok=!receivedCharacter && decodeCharacterChoice(epoch,m.payload,choice);
            if(ok)receivedCharacter=choice;
        }
        else if(m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==5)
        {
            MatchContract remote;
            ok=decodeMatchContract(epoch,m.payload,remote);
            if(!ok || remote!=match || (session.isServer() && !receivedCharacter))matchState=MatchState::Mismatch;
            else matchState=MatchState::Agreed;
        }
        else if(matchState!=MatchState::Agreed) { ++rejected;continue; }
        else if(closing || receivedClose)continue;
        else if (session.isServer() && m.lane==Lane::Gameplay)
        {
            InputBatch b;
            if (decodeInputs(epoch,m.payload,b))
            {
                ok=snapshots.receive(b.snapshots);
                if (ok && snapshots.baselineId()) ok=commands.receive(b,now,[&](uint64_t t){return session.clockSync().toLocal(t,now);});
            }
        }
        else if (!session.isServer() && m.lane==Lane::Gameplay)
        {
            InputFeedback f;std::vector<SoundCue> cues;
            ok=m.payload.size()>4 && m.payload[4]==11 ? decodeSoundFeedback(epoch,m.payload,f,cues) : decodeFeedback(epoch,m.payload,f);
            if(ok) {
                inputs.receive(f);
                for(auto& c:cues)if(c.id>lastSoundReceived) {
                    lastSoundReceived=c.id;if(receivedSounds.size()>=32)receivedSounds.pop_front();receivedSounds.push_back(std::move(c));
                }
            }
        }
        else if (!session.isServer() && m.lane==Lane::Snapshots)
        { SnapshotPart p; if (decodePart(epoch,m.payload,p)) ok=world.receive(p,now); }
        else if (!session.isServer() && m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==6 && !actorOverflow)
        {
            ActorEvent e;
            if(decodeActorEvent(epoch,m.payload,e) && e.id==lastActorReceived+1 && actorEvents.size()<MaxEntities)
            { actorEvents.push_back(std::move(e));lastActorReceived=actorEvents.back().id;ok=true; }
            else { actorOverflow=true;actorEvents.clear();world.resync(); }
        }
        else if(!session.isServer() && m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==7)
        { ActionResult result; if((ok=decodeActionDecision(epoch,m.payload,result)))inputs.receiveDecision(result); }
        else if(!session.isServer() && m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==9)
        { MatchStatus state;if((ok=decodeMatchStatus(epoch,m.payload,state)))receivedStatus=std::move(state); }
        else if (!session.isServer() && m.lane==Lane::Control && m.payload.size()>4 && m.payload[4]==4 && !combatOverflow)
        {
            CombatOutcome c;
            if (decodeCombatOutcome(epoch,m.payload,c) && c.id==lastCombatReceived+1 &&
                c.tick>=lastCombatTick && combatResults.size()<64)
            { combatResults.push_back(c); lastCombatReceived=c.id; lastCombatTick=c.tick; ok=true; }
            else { combatOverflow=true; combatResults.clear(); world.resync(); }
        }
        if (!ok) ++rejected;
    }
    Bytes bytes;
    if(closing || receivedClose)
    {
        session.discardSnapshotParts();
        if(closing && !closeSent && encodeClose(session.worldEpoch(),*closing,bytes) &&
            session.enqueue(Lane::Control,std::move(bytes),0))closeSent=true;
        return;
    }
    if(matchState!=MatchState::Agreed || travelRequest)return;
    if (session.isServer())
    {
        while(!pendingDecisions.empty())
        {
            Bytes decision;
            if(!encodeActionDecision(epoch,pendingDecisions.front(),decision) ||
                !session.enqueue(Lane::Control,std::move(decision),0))break;
            pendingDecisions.pop_front();
        }
        // Combat audio must not wait for the idle 50-ms feedback cadence.
        // Keep ordered two-offer delivery and the existing bounded queue, but
        // pace sound-bearing feedback at no more than one batch per 16 ms.
        // Repeats still precede newer IDs so existing clients retain dedup order.
        while(!pendingSounds.empty() && (now-pendingSounds.front().at>=250 || pendingSounds.front().sends>=2))pendingSounds.pop_front();
        if(now>=nextFeedback || (!pendingSounds.empty() && now>=nextSoundFeedback)) {
            auto feedback=commands.feedback();std::vector<SoundCue> cues;
            for(auto& pending:pendingSounds) {if(cues.size()==4)break;cues.push_back(pending.cue);}
            if(!cues.empty() && feedback.actions.size()>16)feedback.actions.erase(feedback.actions.begin(),feedback.actions.end()-16);
            bool encoded=cues.empty()?encodeFeedback(epoch,feedback,bytes):encodeSoundFeedback(epoch,feedback,cues,bytes);
            if(encoded && session.enqueue(Lane::Gameplay,std::move(bytes),250)) {
                for(size_t i=0;i<cues.size();++i)++pendingSounds[i].sends;
                nextFeedback=now+50;nextSoundFeedback=now+16;
            }
        }
        if (session.snapshotPartsPending()<4)
        {
            auto part=snapshots.poll(now,session.snapshotPartsPending()==0,session.roundTripMs()*2);
            if (snapshots.activeId()!=lastActive) { session.discardSnapshotParts(); lastActive=snapshots.activeId(); }
            if (part && encodePart(epoch,*part,bytes)) session.enqueue(Lane::Snapshots,std::move(bytes),1000);
        }
    }
    else if (sendInputs) flushInputs();
}
void ReplicationPeer::flushInputs()
{
    if(session.isServer() || !session.ready() || !matchAgreed() || travelRequest || closing || receivedClose)return;
    const auto now=session.localTime();
    // A fresh action edge need not wait for the ordinary held-input cadence.
    // Retries retain the cadence; transport pacing and bounded queues still apply.
    if(now<nextFeedback && inputs.newestAction()<=lastActionOffered)return;
    auto b=inputs.batch(now,world.feedback(now));
    if(!world.installed()){b.inputs.clear();b.actions.clear();}
    Bytes bytes;
    if(encodeInputs(replicationEpoch,b,bytes) && session.enqueue(Lane::Gameplay,std::move(bytes),250)) {
        nextFeedback=now+16;
        if(!b.actions.empty())lastActionOffered=inputs.newestAction();
    }
}
}
