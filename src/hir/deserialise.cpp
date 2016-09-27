/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir/serialise.cpp
 * - HIR (De)Serialisation for crate metadata
 */
#include "hir.hpp"
#include "main_bindings.hpp"
#include <serialiser_texttree.hpp>
#include <mir/mir.hpp>
#include <macro_rules/macro_rules.hpp>

namespace {
    
    template<typename T>
    struct D
    {
    };
    
    class HirDeserialiser
    {
        const ::std::string& m_crate_name;
        ::std::istream& m_is;
    public:
        HirDeserialiser(const ::std::string& crate_name, ::std::istream& is):
            m_crate_name( crate_name ),
            m_is(is)
        {}
        
        uint8_t read_u8() {
            uint8_t v;
            m_is.read(reinterpret_cast<char*>(&v), 1);
            if( !m_is ) {
                throw "";
            }
            assert( m_is );
            return v;
        }
        uint16_t read_u16() {
            uint16_t v;
            v  = static_cast<uint16_t>(read_u8());
            v |= static_cast<uint16_t>(read_u8()) << 8;
            return v;
        }
        uint32_t read_u32() {
            uint32_t v;
            v  = static_cast<uint32_t>(read_u8());
            v |= static_cast<uint32_t>(read_u8()) << 8;
            v |= static_cast<uint32_t>(read_u8()) << 16;
            v |= static_cast<uint32_t>(read_u8()) << 24;
            return v;
        }
        uint64_t read_u64() {
            uint64_t v;
            v  = static_cast<uint64_t>(read_u8());
            v |= static_cast<uint64_t>(read_u8()) << 8;
            v |= static_cast<uint64_t>(read_u8()) << 16;
            v |= static_cast<uint64_t>(read_u8()) << 24;
            v |= static_cast<uint64_t>(read_u8()) << 32;
            v |= static_cast<uint64_t>(read_u8()) << 40;
            v |= static_cast<uint64_t>(read_u8()) << 48;
            v |= static_cast<uint64_t>(read_u8()) << 56;
            return v;
        }
        // Variable-length encoded u64 (for array sizes)
        uint64_t read_u64c() {
            auto v = read_u8();
            if( v < (1<<7) ) {
                return static_cast<uint64_t>(v);
            }
            else if( v < 0xC0 ) {
                uint64_t    rv = static_cast<uint64_t>(v & 0x3F) << 16;
                rv |= static_cast<uint64_t>(read_u8()) << 8;
                rv |= static_cast<uint64_t>(read_u8());
                return rv;
            }
            else if( v < 0xFF ) {
                uint64_t    rv = static_cast<uint64_t>(v & 0x3F) << 32;
                rv |= static_cast<uint64_t>(read_u8()) << 24;
                rv |= static_cast<uint64_t>(read_u8()) << 16;
                rv |= static_cast<uint64_t>(read_u8()) << 8;
                rv |= static_cast<uint64_t>(read_u8());
                return rv;
            }
            else {
                return read_u64();
            }
        }
        int64_t read_i64c() {
            uint64_t va = read_u64c();
            bool sign = (va & 0x1) != 0;
            va >>= 1;
            
            if( va == 0 && sign ) {
                return INT64_MIN;
            }
            else if( sign ) {
                return -static_cast<int64_t>(va);
            }
            else {
                return -static_cast<uint64_t>(va);
            }
        }
        double read_double() {
            double v;
            m_is.read(reinterpret_cast<char*>(&v), sizeof v);
            return v;
        }
        unsigned int read_tag() {
            return static_cast<unsigned int>( read_u8() );
        }
        size_t read_count() {
            auto v = read_u8();
            if( v < 0xFE ) {
                return v;
            }
            else if( v == 0xFE ) {
                return read_u16( );
            }
            else /*if( v == 0xFF )*/ {
                return ~0u;
            }
        }
        ::std::string read_string() {
            size_t len = read_u8();
            if( len < 128 ) {
            }
            else {
                len = (len & 0x7F) << 16;
                len |= read_u16();
            }
            ::std::string   rv(len, '\0');
            m_is.read(const_cast<char*>(rv.data()), len);
            return rv;
        }
        bool read_bool() {
            return read_u8() != 0x00;
        }
        
        template<typename V>
        ::std::map< ::std::string,V> deserialise_strmap()
        {
            size_t n = read_count();
            ::std::map< ::std::string, V>   rv;
            //rv.reserve(n);
            for(size_t i = 0; i < n; i ++)
            {
                auto s = read_string();
                rv.insert( ::std::make_pair( mv$(s), D<V>::des(*this) ) );
            }
            return rv;
        }
        template<typename V>
        ::std::unordered_map< ::std::string,V> deserialise_strumap()
        {
            size_t n = read_count();
            ::std::unordered_map< ::std::string, V>   rv;
            //rv.reserve(n);
            for(size_t i = 0; i < n; i ++)
            {
                auto s = read_string();
                DEBUG("- " << s);
                rv.insert( ::std::make_pair( mv$(s), D<V>::des(*this) ) );
            }
            return rv;
        }
        
        template<typename T>
        ::std::vector<T> deserialise_vec()
        {
            size_t n = read_count();
            ::std::vector<T>    rv;
            rv.reserve(n);
            for(size_t i = 0; i < n; i ++)
                rv.push_back( D<T>::des(*this) );
            return rv;
        }
        template<typename T>
        ::std::vector<T> deserialise_vec_c(::std::function<T()> cb)
        {
            size_t n = read_count();
            ::std::vector<T>    rv;
            rv.reserve(n);
            for(size_t i = 0; i < n; i ++)
                rv.push_back( cb() );
            return rv;
        }
        template<typename T>
        ::HIR::VisEnt<T> deserialise_visent()
        {
            return ::HIR::VisEnt<T> { read_bool(), D<T>::des(*this) };
        }
        
        template<typename T>
        ::std::unique_ptr<T> deserialise_ptr() {
            return box$( D<T>::des(*this) );
        }
        
        
        ::HIR::TypeRef deserialise_type();
        ::HIR::SimplePath deserialise_simplepath();
        ::HIR::PathParams deserialise_pathparams();
        ::HIR::GenericPath deserialise_genericpath();
        ::HIR::TraitPath deserialise_traitpath();
        ::HIR::Path deserialise_path();

        ::HIR::GenericParams deserialise_genericparams();
        ::HIR::TypeParamDef deserialise_typaramdef();
        ::HIR::GenericBound deserialise_genericbound();
        
        ::HIR::Crate deserialise_crate();
        ::HIR::Module deserialise_module();
        
        ::HIR::TypeImpl deserialise_typeimpl()
        {
            ::HIR::TypeImpl rv;
            TRACE_FUNCTION_FR("", "impl" << rv.m_params.fmt_args() << " " << rv.m_type);
            
            rv.m_params = deserialise_genericparams();
            rv.m_type = deserialise_type();
            
            size_t method_count = read_count();
            for(size_t i = 0; i < method_count; i ++)
            {
                auto name = read_string();
                rv.m_methods.insert( ::std::make_pair( mv$(name), ::HIR::TypeImpl::VisImplEnt< ::HIR::Function> {
                    read_bool(), read_bool(), deserialise_function()
                    } ) );
            }
            // m_src_module doesn't matter after typeck
            return rv;
        }
        ::HIR::TraitImpl deserialise_traitimpl()
        {
            ::HIR::TraitImpl    rv;
            TRACE_FUNCTION_FR("", "impl" << rv.m_params.fmt_args() << " ?" << rv.m_trait_args << " for " << rv.m_type);
            
            rv.m_params = deserialise_genericparams();
            rv.m_trait_args = deserialise_pathparams();
            rv.m_type = deserialise_type();
            
            
            size_t method_count = read_count();
            for(size_t i = 0; i < method_count; i ++)
            {
                auto name = read_string();
                rv.m_methods.insert( ::std::make_pair( mv$(name), ::HIR::TraitImpl::ImplEnt< ::HIR::Function> {
                    read_bool(), deserialise_function()
                    } ) );
            }
            size_t const_count = read_count();
            for(size_t i = 0; i < const_count; i ++)
            {
                auto name = read_string();
                rv.m_constants.insert( ::std::make_pair( mv$(name), ::HIR::TraitImpl::ImplEnt< ::HIR::ExprPtr> {
                    read_bool(), deserialise_exprptr()
                    } ) );
            }
            size_t type_count = read_count();
            for(size_t i = 0; i < type_count; i ++)
            {
                auto name = read_string();
                rv.m_types.insert( ::std::make_pair( mv$(name), ::HIR::TraitImpl::ImplEnt< ::HIR::TypeRef> {
                    read_bool(), deserialise_type()
                    } ) );
            }
            
            // m_src_module doesn't matter after typeck
            return rv;
        }
        ::HIR::MarkerImpl deserialise_markerimpl()
        {
            return ::HIR::MarkerImpl {
                deserialise_genericparams(),
                deserialise_pathparams(),
                read_bool(),
                deserialise_type()
                };
        }
        
        ::MacroRulesPtr deserialise_macrorulesptr()
        {
            return ::MacroRulesPtr( new MacroRules(deserialise_macrorules()) );
        }
        ::MacroRules deserialise_macrorules()
        {
            ::MacroRules    rv;
            rv.m_exported = true;
            rv.m_rules = deserialise_vec_c< ::MacroRulesArm>( [&](){ return deserialise_macrorulesarm(); });
            return rv;
        }
        ::MacroPatEnt deserialise_macropatent() {
            ::MacroPatEnt   rv {
                read_string(),
                static_cast<unsigned int>(read_count()),
                static_cast< ::MacroPatEnt::Type>(read_tag())
                };
            switch(rv.type)
            {
            case ::MacroPatEnt::PAT_TOKEN:
                rv.tok = deserialise_token();
                break;
            case ::MacroPatEnt::PAT_LOOP:
                rv.subpats = deserialise_vec_c< ::MacroPatEnt>([&](){ return deserialise_macropatent(); });
                break;
            case ::MacroPatEnt::PAT_TT: // :tt
            case ::MacroPatEnt::PAT_PAT:    // :pat
            case ::MacroPatEnt::PAT_IDENT:
            case ::MacroPatEnt::PAT_PATH:
            case ::MacroPatEnt::PAT_TYPE:
            case ::MacroPatEnt::PAT_EXPR:
            case ::MacroPatEnt::PAT_STMT:
            case ::MacroPatEnt::PAT_BLOCK:
            case ::MacroPatEnt::PAT_META:
            case ::MacroPatEnt::PAT_ITEM:
                break;
            default:
                throw "";
            }
            return rv;
        }
        ::MacroRulesArm deserialise_macrorulesarm() {
            ::MacroRulesArm rv;
            rv.m_param_names = deserialise_vec< ::std::string>();
            rv.m_pattern = deserialise_vec_c< ::MacroPatEnt>( [&](){ return deserialise_macropatent(); } );
            rv.m_contents = deserialise_vec_c< ::MacroExpansionEnt>( [&](){ return deserialise_macroexpansionent(); } );
            return rv;
        }
        ::MacroExpansionEnt deserialise_macroexpansionent() {
            switch(read_tag())
            {
            case 0:
                return ::MacroExpansionEnt( deserialise_token() );
            case 1: {
                unsigned int v = static_cast<unsigned int>(read_u8()) << 24;
                return ::MacroExpansionEnt( v | read_count() );
                }
            case 2: {
                auto entries = deserialise_vec_c< ::MacroExpansionEnt>( [&](){ return deserialise_macroexpansionent(); } );
                auto joiner = deserialise_token();
                ::std::map<unsigned int, bool>    variables;
                size_t n = read_count();
                while(n--) {
                    auto idx = static_cast<unsigned int>(read_count());
                    bool flag = read_bool();
                    variables.insert( ::std::make_pair(idx, flag) );
                }
                return ::MacroExpansionEnt::make_Loop({
                    mv$(entries), mv$(joiner), mv$(variables)
                    });
                }
            default:
                throw "";
            }
        }
        
        ::Token deserialise_token() {
            ::Token tok;
            // HACK: Hand off to old serialiser code
            auto s = read_string();
            ::std::stringstream tmp(s);
            {
                Deserialiser_TextTree ser(tmp);
                tok.deserialise( ser );
            }
            return tok;
        }

        ::HIR::Literal deserialise_literal();
        
        ::HIR::ExprPtr deserialise_exprptr()
        {
            ::HIR::ExprPtr  rv;
            if( read_bool() )
            {
                rv.m_mir = deserialise_mir();
            }
            return rv;
        }
        ::MIR::FunctionPointer deserialise_mir();
        ::MIR::BasicBlock deserialise_mir_basicblock();
        ::MIR::Statement deserialise_mir_statement();
        ::MIR::Terminator deserialise_mir_terminator();
        
        ::MIR::LValue deserialise_mir_lvalue() {
            ::MIR::LValue   rv;
            TRACE_FUNCTION_FR("", rv);
            rv = deserialise_mir_lvalue_();
            return rv;
        }
        ::MIR::LValue deserialise_mir_lvalue_()
        {
            switch( read_tag() )
            {
            #define _(x, ...)    case ::MIR::LValue::TAG_##x: return ::MIR::LValue::make_##x( __VA_ARGS__ );
            _(Variable,  static_cast<unsigned int>(read_count()) )
            _(Temporary, { static_cast<unsigned int>(read_count()) } )
            _(Argument,  { static_cast<unsigned int>(read_count()) } )
            _(Static,  deserialise_path() )
            _(Return, {})
            _(Field, {
                box$( deserialise_mir_lvalue() ),
                static_cast<unsigned int>(read_count())
                } )
            _(Deref, { box$( deserialise_mir_lvalue() ) })
            _(Index, {
                box$( deserialise_mir_lvalue() ),
                box$( deserialise_mir_lvalue() )
                } )
            _(Downcast, {
                box$( deserialise_mir_lvalue() ),
                static_cast<unsigned int>(read_count())
                } )
            #undef _
            default:
                throw "";
            }
        }
        ::MIR::RValue deserialise_mir_rvalue()
        {
            TRACE_FUNCTION;
            
            switch( read_tag() )
            {
            #define _(x, ...)    case ::MIR::RValue::TAG_##x: return ::MIR::RValue::make_##x( __VA_ARGS__ );
            _(Use, deserialise_mir_lvalue() )
            _(Constant, deserialise_mir_constant() )
            _(SizedArray, {
                deserialise_mir_lvalue(),
                static_cast<unsigned int>(read_u64c())
                })
            _(Borrow, {
                0, // TODO: Region?
                static_cast< ::HIR::BorrowType>( read_tag() ),
                deserialise_mir_lvalue()
                })
            _(Cast, {
                deserialise_mir_lvalue(),
                deserialise_type()
                })
            _(BinOp, {
                deserialise_mir_lvalue(),
                static_cast< ::MIR::eBinOp>( read_tag() ),
                deserialise_mir_lvalue()
                })
            _(UniOp, {
                deserialise_mir_lvalue(),
                static_cast< ::MIR::eUniOp>( read_tag() )
                })
            _(DstMeta, {
                deserialise_mir_lvalue()
                })
            _(MakeDst, {
                deserialise_mir_lvalue(),
                deserialise_mir_lvalue()
                })
            _(Tuple, {
                deserialise_vec_c< ::MIR::LValue>([&](){ return deserialise_mir_lvalue(); })
                })
            _(Array, {
                deserialise_vec_c< ::MIR::LValue>([&](){ return deserialise_mir_lvalue(); })
                })
            _(Struct, {
                deserialise_genericpath(),
                deserialise_vec_c< ::MIR::LValue>([&](){ return deserialise_mir_lvalue(); })
                })
            #undef _
            default:
                throw "";
            }
        }
        ::MIR::Constant deserialise_mir_constant()
        {
            TRACE_FUNCTION;
            
            switch( read_tag() )
            {
            #define _(x, ...)    case ::MIR::Constant::TAG_##x: DEBUG("- " #x); return ::MIR::Constant::make_##x( __VA_ARGS__ );
            _(Int, read_i64c())
            _(Uint, read_u64c())
            _(Float, read_double())
            _(Bool, read_bool())
            case ::MIR::Constant::TAG_Bytes: {
                ::std::vector<unsigned char>    bytes;
                bytes.resize( read_count() );
                m_is.read( reinterpret_cast<char*>(bytes.data()), bytes.size() );
                return ::MIR::Constant::make_Bytes( mv$(bytes) );
                }
            _(StaticString, read_string() )
            _(Const,  { deserialise_path() } )
            _(ItemAddr, deserialise_path() )
            #undef _
            default:
                throw "";
            }
        }
        
        ::HIR::TypeItem deserialise_typeitem()
        {
            switch( read_tag() )
            {
            case 0:
                return ::HIR::TypeItem( deserialise_simplepath() );
            case 1:
                return ::HIR::TypeItem( deserialise_module() );
            case 2:
                return ::HIR::TypeItem( deserialise_typealias() );
            case 3:
                return ::HIR::TypeItem( deserialise_enum() );
            case 4:
                return ::HIR::TypeItem( deserialise_struct() );
            case 5:
                return ::HIR::TypeItem( deserialise_trait() );
            default:
                throw "";
            }
        }
        ::HIR::ValueItem deserialise_valueitem()
        {
            switch( read_tag() )
            {
            case 0:
                return ::HIR::ValueItem( deserialise_simplepath() );
            case 1:
                return ::HIR::ValueItem( deserialise_constant() );
            case 2:
                return ::HIR::ValueItem( deserialise_static() );
            case 3:
                return ::HIR::ValueItem::make_StructConstant({ deserialise_simplepath() });
            case 4:
                return ::HIR::ValueItem( deserialise_function() );
            case 5:
                return ::HIR::ValueItem::make_StructConstructor({ deserialise_simplepath() });
            default:
                throw "";
            }
        }
        
        // - Value items
        ::HIR::Function deserialise_function()
        {
            TRACE_FUNCTION;
            
            ::HIR::Function rv {
                static_cast< ::HIR::Function::Receiver>( read_tag() ),
                read_string(),
                read_bool(),
                read_bool(),
                deserialise_genericparams(),
                deserialise_fcnargs(),
                deserialise_type(),
                deserialise_exprptr()
                };
            return rv;
        }
        ::std::vector< ::std::pair< ::HIR::Pattern, ::HIR::TypeRef> >   deserialise_fcnargs()
        {
            size_t n = read_count();
            ::std::vector< ::std::pair< ::HIR::Pattern, ::HIR::TypeRef> >    rv;
            rv.reserve(n);
            for(size_t i = 0; i < n; i ++)
                rv.push_back( ::std::make_pair( ::HIR::Pattern{}, deserialise_type() ) );
            DEBUG("rv = " << rv);
            return rv;
        }
        ::HIR::Constant deserialise_constant()
        {
            TRACE_FUNCTION;
            
            return ::HIR::Constant {
                deserialise_genericparams(),
                deserialise_type(),
                deserialise_exprptr(),
                deserialise_literal()
                };
        }
        ::HIR::Static deserialise_static()
        {
            TRACE_FUNCTION;
            
            return ::HIR::Static {
                read_bool(),
                deserialise_type(),
                ::HIR::ExprPtr {}
                };
        }
        
        // - Type items
        ::HIR::TypeAlias deserialise_typealias()
        {
            return ::HIR::TypeAlias {
                deserialise_genericparams(),
                deserialise_type()
                };
        }
        ::HIR::Enum deserialise_enum();
        ::HIR::Enum::Variant deserialise_enumvariant();

        ::HIR::Struct deserialise_struct();
        ::HIR::Trait deserialise_trait();
        
        ::HIR::TraitValueItem deserialise_traitvalueitem()
        {
            switch( read_tag() )
            {
            #define _(x, ...)    case ::HIR::TraitValueItem::TAG_##x: DEBUG("- " #x); return ::HIR::TraitValueItem::make_##x( __VA_ARGS__ );
            _(Constant, deserialise_constant() )
            _(Static,   deserialise_static() )
            _(Function, deserialise_function() )
            #undef _
            default:
                DEBUG("Invalid TraitValueItem tag");
                throw "";
            }
        }
        ::HIR::AssociatedType deserialise_associatedtype()
        {
            return ::HIR::AssociatedType {
                read_bool(),
                "", // TODO: Better lifetime type
                deserialise_vec< ::HIR::TraitPath>(),
                deserialise_type()
                };
        }
    };

    #define DEF_D(ty, ...) \
        struct D< ty > { static ty des(HirDeserialiser& d) { __VA_ARGS__ } };
    
    template<>
    DEF_D( ::std::string,
        return d.read_string(); );
    
    template<typename T>
    DEF_D( ::std::unique_ptr<T>,
        return d.deserialise_ptr<T>(); )
    
    template<typename T, typename U>
    struct D< ::std::pair<T,U> > { static ::std::pair<T,U> des(HirDeserialiser& d) {
        auto a = D<T>::des(d);
        return ::std::make_pair( mv$(a), D<U>::des(d) );
        }};
    
    template<typename T>
    DEF_D( ::HIR::VisEnt<T>,
        return d.deserialise_visent<T>(); )
    
    template<> DEF_D( ::HIR::TypeRef, return d.deserialise_type(); )
    template<> DEF_D( ::HIR::SimplePath, return d.deserialise_simplepath(); )
    template<> DEF_D( ::HIR::GenericPath, return d.deserialise_genericpath(); )
    template<> DEF_D( ::HIR::TraitPath, return d.deserialise_traitpath(); )
    
    template<> DEF_D( ::HIR::TypeParamDef, return d.deserialise_typaramdef(); )
    template<> DEF_D( ::HIR::GenericBound, return d.deserialise_genericbound(); )
    
    template<> DEF_D( ::HIR::ValueItem, return d.deserialise_valueitem(); )
    template<> DEF_D( ::HIR::TypeItem, return d.deserialise_typeitem(); )
    
    template<> DEF_D( ::HIR::Enum::Variant, return d.deserialise_enumvariant(); )
    template<> DEF_D( ::HIR::Literal, return d.deserialise_literal(); )
    
    template<> DEF_D( ::HIR::AssociatedType, return d.deserialise_associatedtype(); )
    template<> DEF_D( ::HIR::TraitValueItem, return d.deserialise_traitvalueitem(); )
    
    template<> DEF_D( ::MIR::LValue, return d.deserialise_mir_lvalue(); )
    template<> DEF_D( ::MIR::Statement, return d.deserialise_mir_statement(); )
    template<> DEF_D( ::MIR::BasicBlock, return d.deserialise_mir_basicblock(); )
    
    template<> DEF_D( ::HIR::TypeImpl, return d.deserialise_typeimpl(); )
    template<> DEF_D( ::MacroRulesPtr, return d.deserialise_macrorulesptr(); )
    
    ::HIR::TypeRef HirDeserialiser::deserialise_type()
    {
        TRACE_FUNCTION;
        switch( read_tag() )
        {
        #define _(x, ...)    case ::HIR::TypeRef::Data::TAG_##x: DEBUG("- "#x); return ::HIR::TypeRef( ::HIR::TypeRef::Data::make_##x( __VA_ARGS__ ) );
        _(Infer, {})
        _(Diverge, {})
        _(Primitive,
            static_cast< ::HIR::CoreType>( read_tag() )
            )
        _(Path, {
            deserialise_path(),
            {}
            })
        _(Generic, {
            read_string(),
            read_u16()
            })
        _(TraitObject, {
            deserialise_traitpath(),
            deserialise_vec< ::HIR::GenericPath>(),
            ""  // TODO: m_lifetime
            })
        _(Array, {
            deserialise_ptr< ::HIR::TypeRef>(),
            ::HIR::ExprPtr(),
            read_u64c()
            })
        _(Slice, {
            deserialise_ptr< ::HIR::TypeRef>()
            })
        _(Tuple,
            deserialise_vec< ::HIR::TypeRef>()
            )
        _(Borrow, {
            static_cast< ::HIR::BorrowType>( read_tag() ),
            deserialise_ptr< ::HIR::TypeRef>()
            })
        _(Pointer, {
            static_cast< ::HIR::BorrowType>( read_tag() ),
            deserialise_ptr< ::HIR::TypeRef>()
            })
        _(Function, {
            read_bool(),
            read_string(),
            deserialise_ptr< ::HIR::TypeRef>(),
            deserialise_vec< ::HIR::TypeRef>()
            })
        #undef _
        default:
            assert(!"Bad TypeRef tag");
        }
    }
    
    ::HIR::SimplePath HirDeserialiser::deserialise_simplepath()
    {
        TRACE_FUNCTION;
        // HACK! If the read crate name is empty, replace it with the name we're loaded with
        auto crate_name = read_string();
        if( crate_name == "" )
            crate_name = m_crate_name;
        return ::HIR::SimplePath {
            mv$(crate_name),
            deserialise_vec< ::std::string>()
            };
    }
    ::HIR::PathParams HirDeserialiser::deserialise_pathparams()
    {
        ::HIR::PathParams   rv;
        TRACE_FUNCTION_FR("", rv);
        rv.m_types = deserialise_vec< ::HIR::TypeRef>();
        return rv;
    }
    ::HIR::GenericPath HirDeserialiser::deserialise_genericpath()
    {
        TRACE_FUNCTION;
        return ::HIR::GenericPath {
            deserialise_simplepath(),
            deserialise_pathparams()
            };
    }
    
    ::HIR::TraitPath HirDeserialiser::deserialise_traitpath()
    {
        return ::HIR::TraitPath {
            deserialise_genericpath(),
            {},
            deserialise_strmap< ::HIR::TypeRef>()
            };
    }
    ::HIR::Path HirDeserialiser::deserialise_path()
    {
        TRACE_FUNCTION;
        switch( read_tag() )
        {
        case 0:
            DEBUG("Generic");
            return ::HIR::Path( deserialise_genericpath() );
        case 1:
            DEBUG("Inherent");
            return ::HIR::Path {
                deserialise_type(),
                read_string(),
                deserialise_pathparams()
                };
        case 2:
            DEBUG("Known");
            return ::HIR::Path {
                deserialise_type(),
                deserialise_genericpath(),
                read_string(),
                deserialise_pathparams()
                };
        default:
            throw "";
        }
    }
    
    ::HIR::GenericParams HirDeserialiser::deserialise_genericparams()
    {
        ::HIR::GenericParams    params;
        params.m_types = deserialise_vec< ::HIR::TypeParamDef>();
        params.m_lifetimes = deserialise_vec< ::std::string>();
        params.m_bounds = deserialise_vec< ::HIR::GenericBound>();
        DEBUG("params = " << params.fmt_args() << ", " << params.fmt_bounds());
        return params;
    }
    ::HIR::TypeParamDef HirDeserialiser::deserialise_typaramdef()
    {
        return ::HIR::TypeParamDef {
            read_string(),
            deserialise_type(),
            read_bool()
            };
    }
    ::HIR::GenericBound HirDeserialiser::deserialise_genericbound()
    {
        switch( read_tag() )
        {
        case 0:
        case 1:
            return ::HIR::GenericBound::make_Lifetime({});
        case 2:
            return ::HIR::GenericBound::make_TraitBound({
                deserialise_type(),
                deserialise_traitpath()
                });
        case 3:
            return ::HIR::GenericBound::make_TypeEquality({
                deserialise_type(),
                deserialise_type()
                });
        default:
            DEBUG("Bad GenericBound tag");
            throw "";
        }
    }

    ::HIR::Enum HirDeserialiser::deserialise_enum()
    {
        TRACE_FUNCTION;
        return ::HIR::Enum {
            deserialise_genericparams(),
            static_cast< ::HIR::Enum::Repr>(read_tag()),
            deserialise_vec< ::std::pair< ::std::string, ::HIR::Enum::Variant> >()
            };
    }
    ::HIR::Enum::Variant HirDeserialiser::deserialise_enumvariant()
    {
        switch( read_tag() )
        {
        case ::HIR::Enum::Variant::TAG_Unit:
            return ::HIR::Enum::Variant::make_Unit({});
        case ::HIR::Enum::Variant::TAG_Value:
            return ::HIR::Enum::Variant::make_Value({
                ::HIR::ExprPtr {},
                deserialise_literal()
                });
        case ::HIR::Enum::Variant::TAG_Tuple:
            return ::HIR::Enum::Variant( deserialise_vec< ::HIR::VisEnt< ::HIR::TypeRef> >() );
        case ::HIR::Enum::Variant::TAG_Struct:
            return ::HIR::Enum::Variant( deserialise_vec< ::std::pair< ::std::string, ::HIR::VisEnt< ::HIR::TypeRef> > >() );
        default:
            throw "";
        }
    }
    ::HIR::Struct HirDeserialiser::deserialise_struct()
    {
        TRACE_FUNCTION;
        auto params = deserialise_genericparams();
        auto repr = static_cast< ::HIR::Struct::Repr>( read_tag() );
        DEBUG("params = " << params.fmt_args() << params.fmt_bounds());
        
        switch( read_tag() )
        {
        case ::HIR::Struct::Data::TAG_Unit:
            DEBUG("Unit");
            return ::HIR::Struct {
                mv$(params), repr,
                ::HIR::Struct::Data::make_Unit( {} )
                };
        case ::HIR::Struct::Data::TAG_Tuple:
            DEBUG("Tuple");
            return ::HIR::Struct {
                mv$(params), repr,
                ::HIR::Struct::Data( deserialise_vec< ::HIR::VisEnt< ::HIR::TypeRef> >() )
                };
        case ::HIR::Struct::Data::TAG_Named:
            DEBUG("Named");
            return ::HIR::Struct {
                mv$(params), repr,
                ::HIR::Struct::Data( deserialise_vec< ::std::pair< ::std::string, ::HIR::VisEnt< ::HIR::TypeRef> > >() )
                };
        default:
            throw "";
        }
    }
    ::HIR::Trait HirDeserialiser::deserialise_trait()
    {
        TRACE_FUNCTION;
        
        ::HIR::Trait rv {
            deserialise_genericparams(),
            "",  // TODO: Better type for lifetime
            deserialise_vec< ::HIR::TraitPath>()
            };
        rv.m_is_marker = read_bool();
        rv.m_types = deserialise_strumap< ::HIR::AssociatedType>();
        rv.m_values = deserialise_strumap< ::HIR::TraitValueItem>();
        return rv;
    }
    
    ::HIR::Literal HirDeserialiser::deserialise_literal()
    {
        switch( read_tag() )
        {
        #define _(x, ...)    case ::HIR::Literal::TAG_##x:   return ::HIR::Literal::make_##x(__VA_ARGS__);
        _(List,   deserialise_vec< ::HIR::Literal>() )
        _(Integer, read_u64() )
        _(Float,   read_double() )
        _(BorrowOf, deserialise_path() )
        _(String,  read_string() )
        #undef _
        default:
            throw "";
        }
    }
    
    ::MIR::FunctionPointer HirDeserialiser::deserialise_mir()
    {
        TRACE_FUNCTION;
        
        ::MIR::Function rv;
        
        rv.named_variables = deserialise_vec< ::HIR::TypeRef>( );
        DEBUG("named_variables = " << rv.named_variables);
        rv.temporaries = deserialise_vec< ::HIR::TypeRef>( );
        DEBUG("temporaries = " << rv.temporaries);
        rv.blocks = deserialise_vec< ::MIR::BasicBlock>( );
        
        return ::MIR::FunctionPointer( new ::MIR::Function(mv$(rv)) );
    }
    ::MIR::BasicBlock HirDeserialiser::deserialise_mir_basicblock()
    {
        TRACE_FUNCTION;
        
        return ::MIR::BasicBlock {
            deserialise_vec< ::MIR::Statement>(),
            deserialise_mir_terminator()
            };
    }
    ::MIR::Statement HirDeserialiser::deserialise_mir_statement()
    {
        TRACE_FUNCTION;
        
        switch( read_tag() )
        {
        case 0:
            return ::MIR::Statement::make_Assign({
                deserialise_mir_lvalue(),
                deserialise_mir_rvalue()
                });
        case 1:
            return ::MIR::Statement::make_Drop({
                read_bool() ? ::MIR::eDropKind::DEEP : ::MIR::eDropKind::SHALLOW,
                deserialise_mir_lvalue()
                });
        default:
            throw "";
        }
    }
    ::MIR::Terminator HirDeserialiser::deserialise_mir_terminator()
    {
        TRACE_FUNCTION;
        
        switch( read_tag() )
        {
        #define _(x, ...)    case ::MIR::Terminator::TAG_##x: return ::MIR::Terminator::make_##x( __VA_ARGS__ );
        _(Incomplete, {})
        _(Return, {})
        _(Diverge, {})
        _(Goto,  static_cast<unsigned int>(read_count()) )
        _(Panic, { static_cast<unsigned int>(read_count()) })
        _(If, {
            deserialise_mir_lvalue(),
            static_cast<unsigned int>(read_count()),
            static_cast<unsigned int>(read_count())
            })
        _(Switch, {
            deserialise_mir_lvalue(),
            deserialise_vec_c<unsigned int>([&](){ return read_count(); })
            })
        _(Call, {
            static_cast<unsigned int>(read_count()),
            static_cast<unsigned int>(read_count()),
            deserialise_mir_lvalue(),
            deserialise_mir_lvalue(),
            deserialise_vec< ::MIR::LValue>()
            })
        #undef _
        default:
            throw "";
        }
    }
    
    ::HIR::Module HirDeserialiser::deserialise_module()
    {
        TRACE_FUNCTION;
        
        ::HIR::Module   rv;
        
        // m_traits doesn't need to be serialised
        rv.m_value_items = deserialise_strumap< ::std::unique_ptr< ::HIR::VisEnt< ::HIR::ValueItem> > >();
        rv.m_mod_items = deserialise_strumap< ::std::unique_ptr< ::HIR::VisEnt< ::HIR::TypeItem> > >();
        
        return rv;
    }
    ::HIR::Crate HirDeserialiser::deserialise_crate()
    {
        ::HIR::Crate    rv;
        
        rv.m_root_module = deserialise_module();
        
        rv.m_type_impls = deserialise_vec< ::HIR::TypeImpl>();
        
        {
            size_t n = read_count();
            for(size_t i = 0; i < n; i ++)
            {
                auto p = deserialise_simplepath();
                rv.m_trait_impls.insert( ::std::make_pair( mv$(p), deserialise_traitimpl() ) );
            }
        }
        {
            size_t n = read_count();
            for(size_t i = 0; i < n; i ++)
            {
                auto p = deserialise_simplepath();
                rv.m_marker_impls.insert( ::std::make_pair( mv$(p), deserialise_markerimpl() ) );
            }
        }
        
        rv.m_exported_macros = deserialise_strumap< ::MacroRulesPtr>();
        rv.m_lang_items = deserialise_strumap< ::HIR::SimplePath>();
        
        return rv;
    }
}

::HIR::CratePtr HIR_Deserialise(const ::std::string& filename, const ::std::string& loaded_name)
{
    ::std::ifstream in(filename);
    HirDeserialiser  s { loaded_name, in };
    
    try
    {
        ::HIR::Crate    rv = s.deserialise_crate();
        
        return ::HIR::CratePtr( mv$(rv) );
    }
    catch(int)
    { ::std::abort(); }
    #if 0
    catch(const char*)
    {
        ::std::cerr << "Unable to load crate from " << filename << ": Deserialisation failure" << ::std::endl;
        ::std::abort();
        //return ::HIR::CratePtr();
    }
    #endif
}

