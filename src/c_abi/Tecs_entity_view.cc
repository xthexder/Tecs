
#include <Tecs_entity_view.hh>
#include <c_abi/Tecs_entity_view.hh>

extern "C" {

TECS_EXPORT size_t Tecs_entity_view_storage_size(const TecsEntityView *view) {
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    return storage->size();
}

TECS_EXPORT const TecsEntity *Tecs_entity_view_begin(const TecsEntityView *view) {
    static_assert(sizeof(TecsEntity) == sizeof(Tecs::Entity));
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    if (storage->size() == 0) return nullptr;
    return reinterpret_cast<const TecsEntity *>(&*storage->begin());
}

TECS_EXPORT const TecsEntity *Tecs_entity_view_end(const TecsEntityView *view) {
    static_assert(sizeof(TecsEntity) == sizeof(Tecs::Entity));
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    if (storage->size() == 0) return nullptr;
    return reinterpret_cast<const TecsEntity *>(&*storage->end());
}
}
