module;
#include <flecs.h>

export module dependency.ecs;

export namespace flecs
{
using ::flecs::entity;
using ::flecs::entity_t;
using ::flecs::query;
using ::flecs::world;
} // namespace flecs

export using ::EcsChildOf;
export using ::EcsIsA;
export using ::EcsParent;
export using ::EcsPrefab;
export using ::ecs_children;
export using ::ecs_children_next;
export using ::ecs_get_parent;
export using ::ecs_init;
