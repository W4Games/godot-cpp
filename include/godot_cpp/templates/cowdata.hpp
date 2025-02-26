/**************************************************************************/
/*  cowdata.hpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef GODOT_COWDATA_HPP
#define GODOT_COWDATA_HPP

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>

#include <cstring>
#include <new>
#include <type_traits>

namespace godot {

template <class T>
class Vector;

template <class T, class V>
class VMap;

template <class T>
class CharStringT;

SAFE_NUMERIC_TYPE_PUN_GUARANTEES(uint64_t)

// Silence a false positive warning (see GH-52119).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wplacement-new"
#endif

template <class T>
class CowData {
	template <class TV>
	friend class Vector;

	template <class TV, class VV>
	friend class VMap;

	template <class TS>
	friend class CharStringT;

public:
	typedef int64_t Size;
	typedef uint64_t USize;
	static constexpr USize MAX_INT = INT64_MAX;

private:
	// Function to find the next power of 2 to an integer.
	static _FORCE_INLINE_ USize next_po2(USize x) {
		if (x == 0) {
			return 0;
		}

		--x;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		if (sizeof(USize) == 8) {
			x |= x >> 32;
		}

		return ++x;
	}

	static constexpr USize ALLOC_PAD = sizeof(USize) * 2; // For size and atomic refcount.

	mutable T *_ptr = nullptr;

	// internal helpers

	_FORCE_INLINE_ SafeNumeric<USize> *_get_refcount() const {
		if (!_ptr) {
			return nullptr;
		}

		return reinterpret_cast<SafeNumeric<USize> *>(_ptr) - 2;
	}

	_FORCE_INLINE_ USize *_get_size() const {
		if (!_ptr) {
			return nullptr;
		}

		return reinterpret_cast<USize *>(_ptr) - 1;
	}

	_FORCE_INLINE_ USize _get_alloc_size(USize p_elements) const {
		return next_po2(p_elements * sizeof(T));
	}

	_FORCE_INLINE_ bool _get_alloc_size_checked(USize p_elements, USize *out) const {
		if (unlikely(p_elements == 0)) {
			*out = 0;
			return true;
		}
#if defined(__GNUC__) && defined(IS_32_BIT)
		USize o;
		USize p;
		if (__builtin_mul_overflow(p_elements, sizeof(T), &o)) {
			*out = 0;
			return false;
		}
		*out = next_po2(o);
		if (__builtin_add_overflow(o, static_cast<USize>(32), &p)) {
			return false; // No longer allocated here.
		}
#else
		// Speed is more important than correctness here, do the operations unchecked
		// and hope for the best.
		*out = _get_alloc_size(p_elements);
#endif
		return *out;
	}

	void _unref(void *p_data);
	void _ref(const CowData *p_from);
	void _ref(const CowData &p_from);
	USize _copy_on_write();

public:
	void operator=(const CowData<T> &p_from) { _ref(p_from); }

	_FORCE_INLINE_ T *ptrw() {
		_copy_on_write();
		return _ptr;
	}

	_FORCE_INLINE_ const T *ptr() const {
		return _ptr;
	}

	_FORCE_INLINE_ Size size() const {
		USize *size = (USize *)_get_size();
		if (size) {
			return *size;
		} else {
			return 0;
		}
	}

	_FORCE_INLINE_ void clear() { resize(0); }
	_FORCE_INLINE_ bool is_empty() const { return _ptr == nullptr; }

	_FORCE_INLINE_ void set(Size p_index, const T &p_elem) {
		ERR_FAIL_INDEX(p_index, size());
		_copy_on_write();
		_ptr[p_index] = p_elem;
	}

	_FORCE_INLINE_ T &get_m(Size p_index) {
		CRASH_BAD_INDEX(p_index, size());
		_copy_on_write();
		return _ptr[p_index];
	}

	_FORCE_INLINE_ const T &get(Size p_index) const {
		CRASH_BAD_INDEX(p_index, size());

		return _ptr[p_index];
	}

	template <bool p_ensure_zero = false>
	Error resize(Size p_size);

	_FORCE_INLINE_ void remove_at(Size p_index) {
		ERR_FAIL_INDEX(p_index, size());
		T *p = ptrw();
		Size len = size();
		for (Size i = p_index; i < len - 1; i++) {
			p[i] = p[i + 1];
		}

		resize(len - 1);
	}

	Error insert(Size p_pos, const T &p_val) {
		ERR_FAIL_INDEX_V(p_pos, size() + 1, ERR_INVALID_PARAMETER);
		resize(size() + 1);
		for (Size i = (size() - 1); i > p_pos; i--) {
			set(i, get(i - 1));
		}
		set(p_pos, p_val);

		return OK;
	}

	Size find(const T &p_val, Size p_from = 0) const;
	Size rfind(const T &p_val, Size p_from = -1) const;
	Size count(const T &p_val) const;

	_FORCE_INLINE_ CowData() {}
	_FORCE_INLINE_ ~CowData();
	_FORCE_INLINE_ CowData(CowData<T> &p_from) { _ref(p_from); };
};

template <class T>
void CowData<T>::_unref(void *p_data) {
	if (!p_data) {
		return;
	}

	SafeNumeric<USize> *refc = _get_refcount();

	if (refc->decrement() > 0) {
		return; // still in use
	}
	// clean up

	if (!std::is_trivially_destructible<T>::value) {
		USize *count = _get_size();
		T *data = (T *)(count + 1);

		for (USize i = 0; i < *count; ++i) {
			// call destructors
			data[i].~T();
		}
	}

	// free mem
	Memory::free_static(((uint8_t *)p_data) - ALLOC_PAD, false);
}

template <class T>
typename CowData<T>::USize CowData<T>::_copy_on_write() {
	if (!_ptr) {
		return 0;
	}

	SafeNumeric<USize> *refc = _get_refcount();

	USize rc = refc->get();
	if (unlikely(rc > 1)) {
		/* in use by more than me */
		USize current_size = *_get_size();

		USize *mem_new = (USize *)Memory::alloc_static(_get_alloc_size(current_size) + ALLOC_PAD, false);
		mem_new += 2;

		new (mem_new - 2) SafeNumeric<USize>(1); //refcount
		*(mem_new - 1) = current_size; //size

		T *_data = (T *)(mem_new);

		// initialize new elements
		if (std::is_trivially_copyable<T>::value) {
			memcpy(mem_new, _ptr, current_size * sizeof(T));

		} else {
			for (USize i = 0; i < current_size; i++) {
				memnew_placement(&_data[i], T(_ptr[i]));
			}
		}

		_unref(_ptr);
		_ptr = _data;

		rc = 1;
	}
	return rc;
}

template <class T>
template <bool p_ensure_zero>
Error CowData<T>::resize(Size p_size) {
	ERR_FAIL_COND_V(p_size < 0, ERR_INVALID_PARAMETER);

	Size current_size = size();

	if (p_size == current_size) {
		return OK;
	}

	if (p_size == 0) {
		// wants to clean up
		_unref(_ptr);
		_ptr = nullptr;
		return OK;
	}

	// possibly changing size, copy on write
	USize rc = _copy_on_write();

	USize current_alloc_size = _get_alloc_size(current_size);
	USize alloc_size;
	ERR_FAIL_COND_V(!_get_alloc_size_checked(p_size, &alloc_size), ERR_OUT_OF_MEMORY);

	if (p_size > current_size) {
		if (alloc_size != current_alloc_size) {
			if (current_size == 0) {
				// alloc from scratch
				USize *ptr = (USize *)Memory::alloc_static(alloc_size + ALLOC_PAD, false);
				ptr += 2;
				ERR_FAIL_NULL_V(ptr, ERR_OUT_OF_MEMORY);
				*(ptr - 1) = 0; //size, currently none
				new (ptr - 2) SafeNumeric<USize>(1); //refcount

				_ptr = (T *)ptr;

			} else {
				USize *_ptrnew = (USize *)Memory::realloc_static(((uint8_t *)_ptr) - ALLOC_PAD, alloc_size + ALLOC_PAD, false);
				ERR_FAIL_NULL_V(_ptrnew, ERR_OUT_OF_MEMORY);
				_ptrnew += 2;
				new (_ptrnew - 2) SafeNumeric<USize>(rc); //refcount

				_ptr = (T *)(_ptrnew);
			}
		}

		// construct the newly created elements

		if (!std::is_trivially_constructible<T>::value) {
			for (Size i = *_get_size(); i < p_size; i++) {
				memnew_placement(&_ptr[i], T);
			}
		} else if (p_ensure_zero) {
			memset((void *)(_ptr + current_size), 0, (p_size - current_size) * sizeof(T));
		}

		*_get_size() = p_size;

	} else if (p_size < current_size) {
		if (!std::is_trivially_destructible<T>::value) {
			// deinitialize no longer needed elements
			for (USize i = p_size; i < *_get_size(); i++) {
				T *t = &_ptr[i];
				t->~T();
			}
		}

		if (alloc_size != current_alloc_size) {
			USize *_ptrnew = (USize *)Memory::realloc_static(((uint8_t *)_ptr) - ALLOC_PAD, alloc_size + ALLOC_PAD, false);
			ERR_FAIL_NULL_V(_ptrnew, ERR_OUT_OF_MEMORY);
			_ptrnew += 2;
			new (_ptrnew - 2) SafeNumeric<USize>(rc); //refcount

			_ptr = (T *)(_ptrnew);
		}

		*_get_size() = p_size;
	}

	return OK;
}

template <class T>
typename CowData<T>::Size CowData<T>::find(const T &p_val, Size p_from) const {
	Size ret = -1;

	if (p_from < 0 || size() == 0) {
		return ret;
	}

	for (Size i = p_from; i < size(); i++) {
		if (get(i) == p_val) {
			ret = i;
			break;
		}
	}

	return ret;
}

template <class T>
typename CowData<T>::Size CowData<T>::rfind(const T &p_val, Size p_from) const {
	const Size s = size();

	if (p_from < 0) {
		p_from = s + p_from;
	}
	if (p_from < 0 || p_from >= s) {
		p_from = s - 1;
	}

	for (Size i = p_from; i >= 0; i--) {
		if (get(i) == p_val) {
			return i;
		}
	}
	return -1;
}

template <class T>
typename CowData<T>::Size CowData<T>::count(const T &p_val) const {
	Size amount = 0;
	for (Size i = 0; i < size(); i++) {
		if (get(i) == p_val) {
			amount++;
		}
	}
	return amount;
}

template <class T>
void CowData<T>::_ref(const CowData *p_from) {
	_ref(*p_from);
}

template <class T>
void CowData<T>::_ref(const CowData &p_from) {
	if (_ptr == p_from._ptr) {
		return; // self assign, do nothing.
	}

	_unref(_ptr);
	_ptr = nullptr;

	if (!p_from._ptr) {
		return; // nothing to do
	}

	if (p_from._get_refcount()->conditional_increment() > 0) { // could reference
		_ptr = p_from._ptr;
	}
}

template <class T>
CowData<T>::~CowData() {
	_unref(_ptr);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace godot

#endif // GODOT_COWDATA_HPP
