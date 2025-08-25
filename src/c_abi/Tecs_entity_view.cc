
#include <Tecs_entity_view.hh>
#include <c_abi/Tecs_entity_view.hh>

extern "C" {

TECS_EXPORT size_t Tecs_entity_view_storage_size(const tecs_entity_view_t *view) {
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    return storage->size();
}

TECS_EXPORT const tecs_entity_t *Tecs_entity_view_begin(const tecs_entity_view_t *view) {
    static_assert(sizeof(tecs_entity_t) == sizeof(Tecs::Entity));
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    if (storage->size() == 0) return nullptr;
    return reinterpret_cast<const tecs_entity_t *>(&*storage->begin());
}

TECS_EXPORT const tecs_entity_t *Tecs_entity_view_end(const tecs_entity_view_t *view) {
    static_assert(sizeof(tecs_entity_t) == sizeof(Tecs::Entity));
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    if (storage->size() == 0) return nullptr;
    return reinterpret_cast<const tecs_entity_t *>(&*storage->end());
}
} // extern "C"
