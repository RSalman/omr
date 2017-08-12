/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 2017, 2017
 *
 *  This program and the accompanying materials are made available
 *  under the terms of the Eclipse Public License v1.0 and
 *  Apache License v2.0 which accompanies this distribution.
 *
 *      The Eclipse Public License is available at
 *      http://www.eclipse.org/legal/epl-v10.html
 *
 *      The Apache License v2.0 is available at
 *      http://www.opensource.org/licenses/apache2.0.php
 *
 * Contributors:
 *    Multiple authors (IBM Corp.) - initial implementation and documentation
 ******************************************************************************/

#ifndef AST_HPP
#define AST_HPP

#include "ast.h"

#include <type_traits>
#include <assert.h>
#include <cstring>

struct ASTValue {
    private:
    ASTValueType _type;
    union {
        uint64_t integer;
        double floatingPoint;
        const char* str;
    } _value;

    public:
    ASTValue* next;

    using Integer_t = uint64_t;
    using Double_t = double;
    using String_t = const char *;

    explicit ASTValue(Integer_t v) : _type{Int64}, next{nullptr} { _value.integer = v; }
    explicit ASTValue(Double_t v) : _type{Double}, next{nullptr} { _value.floatingPoint = v; }
    explicit ASTValue(String_t v) : _type{String}, next{nullptr} { _value.str = v; }

    template <typename T>
    typename std::enable_if<std::is_integral<T>::value, T>::type get() const {
        assert(Int64 == _type);
        return static_cast<T>(_value.integer);
    }

    template <typename T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type get() const {
        assert(Double == _type);
        return static_cast<T>(_value.floatingPoint);
    }

    template <typename T>
    typename std::enable_if<std::is_same<String_t, T>::value, T>::type get() const {
        assert(String == _type);
        return _value.str;
    }

    template <typename T>
    typename std::enable_if<std::is_integral<T>::value, bool>::type isCompatibleWith() const {
        return Int64 == _type;
    }
    template <typename T>
    typename std::enable_if<std::is_floating_point<T>::value, bool>::type isCompatibleWith() const {
        return Double == _type;
    }
    template <typename T>
    typename std::enable_if<std::is_same<String_t, T>::value, bool>::type isCompatibleWith() const {
        return String == _type;
    }

    ASTValueType getType() const { return _type; }
};

inline bool operator == (const ASTValue& lhs, const ASTValue& rhs) {
   if (lhs.getType() == rhs.getType()) {
      switch (lhs.getType()) {
         case Int64: return lhs.get<ASTValue::Integer_t>() == rhs.get<ASTValue::Integer_t>();
         case Double: return lhs.get<ASTValue::Double_t>() == rhs.get<ASTValue::Double_t>();
         case String: return std::strcmp(lhs.get<ASTValue::String_t>(), rhs.get<ASTValue::String_t>()) == 0;
      }
   }
   return false;
}

struct ASTNodeArg {
    const char* name;
    ASTValue* value;
    ASTNodeArg* next;
};

struct ASTNode {
    const char* name;
    ASTNodeArg* args;
    ASTNode* children;
    ASTNode* next;
};

#endif // AST_HPP
