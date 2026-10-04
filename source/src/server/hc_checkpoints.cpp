// server.dll: half-life's saves roll minecraft back too (see hc_checkpoints.h).

#include "cbase.h"

#include "tier0/valve_minmax_off.h"
#include <cstdint>
#include <ctime>

#include "core/hc_link.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "server/hc_checkpoints.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	constexpr char CHECKPOINT_CLASSNAME[] = "halfcraft_checkpoint";
}

// the checkpoint id of the save this level came from (or was last saved into)
class CHalfCraftCheckpoint : public CLogicalEntity
{
public:
	DECLARE_CLASS(CHalfCraftCheckpoint, CLogicalEntity);
	DECLARE_DATADESC();

	// it belongs to its level: a transition doesn't carry it along with the player
	int ObjectCaps() override { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	std::uint64_t id() const { return (std::uint64_t(static_cast<std::uint32_t>(m_iHigh)) << 32) | static_cast<std::uint32_t>(m_iLow); }

	void set_id(std::uint64_t id)
	{
		m_iLow = static_cast<int>(id & 0xFFFFFFFFu);
		m_iHigh = static_cast<int>(id >> 32);
	}

private:
	int m_iLow = 0;
	int m_iHigh = 0;
};

LINK_ENTITY_TO_CLASS(halfcraft_checkpoint, CHalfCraftCheckpoint);

BEGIN_DATADESC(CHalfCraftCheckpoint)
	DEFINE_FIELD(m_iLow, FIELD_INTEGER),
	DEFINE_FIELD(m_iHigh, FIELD_INTEGER),
END_DATADESC()

namespace halfcraft
{
	namespace
	{
		CHalfCraftCheckpoint* find_checkpoint()
		{
			return static_cast<CHalfCraftCheckpoint*>(gEntList.FindEntityByClassname(nullptr, CHECKPOINT_CLASSNAME));
		}

		/// unique across sessions: the time, and a counter for saves within the same second
		std::uint64_t next_id()
		{
			static std::uint32_t counter = 0;
			return (std::uint64_t(std::time(nullptr)) << 20) | (++counter & 0xFFFFFu);
		}

		void push(proto::InputType type, std::uint64_t id)
		{
			static PushInputFn push_input = nullptr;
			if (!push_input) {
				push_input = reinterpret_cast<PushInputFn>(find_export("client.dll", HC_PUSH_INPUT_EXPORT));
				if (!push_input) {
					return;
				}
			}
			push_input(type, 0, static_cast<int>(id & 0xFFFFFFFFu), static_cast<int>(id >> 32), 0);
		}
	}

	void Checkpoints::on_save()
	{
		CHalfCraftCheckpoint* checkpoint = find_checkpoint();
		if (!checkpoint) {
			log_warning("saving without a checkpoint entity: minecraft won't roll back to this save");
			return;
		}
		const std::uint64_t id = next_id();
		checkpoint->set_id(id);
		push(proto::kInCheckpoint, id);
		log_info("checkpoint %llx", static_cast<unsigned long long>(id));
	}

	void Checkpoints::on_level_loaded()
	{
		CHalfCraftCheckpoint* checkpoint = find_checkpoint();
		if (gpGlobals->eLoadType == MapLoad_LoadGame) {
			if (checkpoint && checkpoint->id() != 0) {
				push(proto::kInRestore, checkpoint->id());
				log_info("save loaded: minecraft rolls back to checkpoint %llx", static_cast<unsigned long long>(checkpoint->id()));
			} else {
				log_info("save loaded without a checkpoint (made before halfcraft rolled back): minecraft stays as it is");
			}
		}
		if (!checkpoint) {
			checkpoint = static_cast<CHalfCraftCheckpoint*>(CreateEntityByName(CHECKPOINT_CLASSNAME));
			if (checkpoint) {
				DispatchSpawn(checkpoint);
			} else {
				log_error("couldn't create %s", CHECKPOINT_CLASSNAME);
			}
		}
	}
}
