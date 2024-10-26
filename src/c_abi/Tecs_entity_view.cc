
#include <Tecs_entity_view.hh>
#include <c_abi/Tecs_entity_view.hh>

thread_local size_t Tecs::abi::viewInvalidationCounter;

extern "C" {

size_t Tecs_entity_view_storage_size(const TecsEntityView *view) {
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    return storage->size();
}

const TecsEntity *Tecs_entity_view_begin(const TecsEntityView *view) {
    static_assert(sizeof(TecsEntity) == sizeof(Tecs::Entity));
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    return reinterpret_cast<const TecsEntity *>(&*storage->begin());
}

const TecsEntity *Tecs_entity_view_end(const TecsEntityView *view) {
    static_assert(sizeof(TecsEntity) == sizeof(Tecs::Entity));
    auto *storage = static_cast<decltype(Tecs::EntityView::storage)>(view->storage);
    return reinterpret_cast<const TecsEntity *>(&*storage->end());
}
}
