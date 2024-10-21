#pragma once

#include "Tecs_entity.hh"
#include "Tecs_entity_view.h"

#include <iterator>
#include <limits>
#include <vector>

namespace Tecs::abi {
    class EntityView {
    public:
        typedef const Entity element_type;
        typedef Entity value_type;
        typedef size_t size_type;
        typedef std::ptrdiff_t difference_type;

        typedef const Entity *pointer;
        typedef const Entity &reference;

        class iterator {
        public:
            typedef std::ptrdiff_t difference_type;
            typedef Entity value_type;
            typedef const Entity *pointer;
            typedef const Entity &reference;
            typedef std::random_access_iterator_tag iterator_category;

            iterator(const EntityView &view, size_t index = 0) : view(view), i(index) {}

            inline reference operator*() const {
#ifndef TECS_UNCHECKED_MODE
                if (i < view.start_index || i >= view.end_index) {
                    throw std::runtime_error("EntityView::iterator::operator*: index out of bounds");
                }
#endif
                return (*view.storage)[i];
            }

            inline pointer operator->() const {
#ifndef TECS_UNCHECKED_MODE
                if (i < view.start_index || i >= view.end_index) {
                    throw std::runtime_error("EntityView::iterator::operator->: index out of bounds");
                }
#endif
                return &(*view.storage)[i];
            }

            inline reference operator[](difference_type n) const {
                size_t index = i + n;
#ifndef TECS_UNCHECKED_MODE
                if (index < view.start_index || index >= view.end_index) {
                    throw std::runtime_error("EntityView::iterator::operator[]: index out of bounds");
                }
#endif
                return (*view.storage)[index];
            }

            inline iterator &operator++() noexcept {
                i++;
                return *this;
            }

            inline iterator &operator--() noexcept {
                i--;
                return *this;
            }

            inline iterator operator++(int) noexcept {
                iterator tmp = *this;
                i++;
                return tmp;
            }

            inline iterator operator--(int) noexcept {
                iterator tmp = *this;
                i--;
                return tmp;
            }

            inline iterator operator+(difference_type n) const noexcept {
                return iterator(view, i + n);
            }

            inline iterator operator-(difference_type n) const noexcept {
                return iterator(view, i - n);
            }

            inline iterator &operator+=(difference_type n) noexcept {
                i += n;
                return *this;
            }

            inline iterator &operator-=(difference_type n) noexcept {
                i -= n;
                return *this;
            }

            inline bool operator==(const iterator &other) const noexcept {
                return view.storage == other.view.storage && i == other.i;
            }

            inline bool operator!=(const iterator &other) const noexcept {
                return view.storage != other.view.storage || i != other.i;
            }

            const EntityView &view;
            size_t i;
        };

        typedef std::reverse_iterator<iterator> reverse_iterator;

        EntityView() {}
        EntityView(const TecsEntityView &view) : c_view(view) {
#ifndef TECS_UNCHECKED_MODE
            if (view.storage == nullptr) {
                throw std::runtime_error("EntityView storage is null");
            }
            size_t storage_size = Tecs_entity_view_size(&view);
            if (view.start_index > storage_size) {
                throw std::runtime_error("EntityView start index out of range: " + std::to_string(view.start_index));
            } else if (view.end_index > storage_size) {
                throw std::runtime_error("EntityView end index out of range: " + std::to_string(view.end_index));
            } else if (view.start_index > view.end_index) {
                throw std::runtime_error("EntityView start index is past end index: " +
                                         std::to_string(view.start_index) + " > " + std::to_string(view.end_index));
            }
#endif
        }

        inline iterator begin() const noexcept {
            return iterator(*this, c_view.start_index);
        }

        inline iterator end() const noexcept {
            return iterator(*this, c_view.end_index);
        }

        inline reverse_iterator rbegin() const noexcept {
            return reverse_iterator(iterator{*this, c_view.end_index});
        }

        inline reverse_iterator rend() const noexcept {
            return reverse_iterator(iterator{*this, c_view.start_index + 1});
        }

        inline reference operator[](size_type index) const {
#ifndef TECS_UNCHECKED_MODE
            if (index < c_view.start_index || index >= c_view.end_index) {
                throw std::runtime_error("EntityView index out of range: " + std::to_string(index));
            }
#endif
            return (*storage)[index];
        }

        inline size_type size() const noexcept {
            return c_view.end_index - c_view.start_index;
        }

        inline bool empty() const noexcept {
            return c_view.end_index <= c_view.start_index;
        }

        inline EntityView subview(size_type offset, size_type count = std::numeric_limits<size_type>::max()) const {
#ifndef TECS_UNCHECKED_MODE
            if (storage == nullptr) throw std::runtime_error("EntityView::subview storage is null");
#endif
            return EntityView(*storage,
                c_view.start_index + offset,
                std::min(c_view.end_index, c_view.start_index + offset + count));
        }

        TecsEntityView c_view;
    };
}; // namespace Tecs::abi
