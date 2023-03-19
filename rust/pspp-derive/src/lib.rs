// PSPP - a program for statistical analysis.
// Copyright (C) 2025 Free Software Foundation, Inc.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program.  If not, see <http://www.gnu.org/licenses/>.

use proc_macro::TokenStream;
use proc_macro2::{Literal, TokenStream as TokenStream2};
use quote::{format_ident, quote, ToTokens};
use syn::{spanned::Spanned, Attribute, DataEnum, DataStruct, DeriveInput, Error, Fields, Token};

#[proc_macro_derive(FromTokens, attributes(pspp))]
pub fn from_tokens_derive(input: TokenStream) -> TokenStream {
    // Construct a representation of Rust code as a syntax tree
    // that we can manipulate
    let ast: DeriveInput = syn::parse(input).unwrap();

    match parse_derive_input(ast) {
        Ok(output) => output.into(),
        Err(error) => error.to_compile_error().into(),
    }
}

fn parse_derive_input(ast: DeriveInput) -> Result<TokenStream2, Error> {
    match &ast.data {
        syn::Data::Enum(e) => derive_enum(&ast, e),
        syn::Data::Struct(s) => derive_struct(&ast, s),
        syn::Data::Union(_) => Err(Error::new(
            ast.span(),
            "Only struct and enums may currently be derived",
        )),
    }
}

fn derive_enum(ast: &DeriveInput, e: &DataEnum) -> Result<TokenStream2, Error> {
    let struct_attrs = StructAttrs::parse(&ast.attrs)?;
    let mut body = TokenStream2::new();
    let name = &ast.ident;
    let (impl_generics, ty_generics, where_clause) = ast.generics.split_for_impl();
    for variant in &e.variants {
        let ident = &variant.ident;
        let field_attrs = FieldAttrs::parse(&variant.attrs)?;
        let selector = field_attrs.selector.unwrap_or(struct_attrs.selector);
        let construction =
            construct_fields(&variant.fields, quote! { #name::#ident }, selector, None);
        let fnname = format_ident!("construct_{ident}");
        body.extend(quote! {
            fn #fnname #impl_generics(input: &TokenSlice) -> ParseResult<#name #ty_generics> #where_clause { let input = input.clone();  #construction }
        });
    }

    for variant in &e.variants {
        let ident = &variant.ident;
        let fnname = format_ident!("construct_{ident}");
        let field_attrs = FieldAttrs::parse(&variant.attrs)?;
        let selector = field_attrs.selector.unwrap_or(struct_attrs.selector);
        if selector {
            let ident_string = ident.to_string();
            let select_expr = if let Some(syntax) = &field_attrs.syntax {
                quote! { input.skip_syntax(#syntax) }
            } else if ident_string.eq_ignore_ascii_case("all") {
                quote! { input.skip(&Token::Punct(Punct::All))}
            } else {
                quote! { input.skip_keyword(#ident_string)}
            };
            body.extend(quote! { if let Some(input) = #select_expr { return #fnname(&input); } });
        } else {
            body.extend(quote! {
                let result = #fnname(&input);
                if let Ok(_) | Err(ParseError::Error(_)) = result {
                    return result;
                }
            });
        }
    }
    body.extend(quote! { Err(ParseError::Mismatch(input.error("Syntax error.").into())) });

    let output = quote! {
        impl #impl_generics FromTokens for #name #ty_generics #where_clause {
            fn from_tokens(input: &TokenSlice) -> ParseResult<Self> {
                #body
            }
        }
    };
    //println!("{output}");
    Ok(output)
}

fn construct_fields(
    fields: &Fields,
    name: impl ToTokens,
    mismatch_to_error: bool,
    syntax: Option<&Literal>,
) -> impl ToTokens {
    let mut construction = TokenStream2::new();
    if !fields.is_empty() {
        construction
            .extend(quote! { let mut diagnostics = crate::command::Diagnostics::default(); });
    }
    let convert = if mismatch_to_error {
        quote! { .mismatch_to_error() }
    } else {
        quote! {}
    };
    for (index, _field) in fields.iter().enumerate() {
        let varname = format_ident!("field{index}");
        construction
            .extend(quote! { let (#varname, input) = FromTokens::from_tokens(&input) #convert ?.take_diagnostics(&mut diagnostics); });
    }
    match fields {
        Fields::Named(named) => {
            let mut body = TokenStream2::new();
            for (index, field) in named.named.iter().enumerate() {
                let varname = format_ident!("field{index}");
                let field_name = &field.ident;
                body.extend(quote! { #field_name: #varname, });
            }
            quote! { #construction Ok(Parsed::new(#name { #body }, input, diagnostics)) }
        }
        Fields::Unnamed(unnamed) => {
            let mut body = TokenStream2::new();
            for (index, _field) in unnamed.unnamed.iter().enumerate() {
                let varname = format_ident!("field{index}");
                body.extend(quote! { #varname, });
            }
            quote! { #construction Ok(Parsed::new(#name ( #body ), input, diagnostics)) }
        }
        Fields::Unit => {
            if let Some(syntax) = syntax {
                quote! { crate::command::parse_syntax(input, #syntax).map(|p| p.map(|()| #name)) }
            } else {
                quote! { Ok(Parsed::ok(#name, input)) }
            }
        }
    }
}

fn derive_struct(ast: &DeriveInput, s: &DataStruct) -> Result<TokenStream2, Error> {
    let struct_attrs = StructAttrs::parse(&ast.attrs)?;
    let name = &ast.ident;
    let syntax = if let Some(syntax) = struct_attrs.syntax.as_ref() {
        syntax.clone()
    } else {
        Literal::string(&name.to_string())
    };
    let construction = construct_fields(&s.fields, quote! {#name}, false, Some(&syntax));
    let (impl_generics, ty_generics, where_clause) = ast.generics.split_for_impl();
    let output = quote! {
        impl #impl_generics FromTokens for #name #ty_generics #where_clause {
            fn from_tokens(input: &TokenSlice) -> ParseResult<Self> {
                #construction
            }
        }
    };
    //println!("{output}");
    Ok(output)
}

#[derive(Default)]
struct FieldAttrs {
    syntax: Option<Literal>,
    selector: Option<bool>,
}

impl FieldAttrs {
    fn parse(attributes: &[Attribute]) -> Result<Self, Error> {
        let mut field_attrs = Self::default();
        for attr in attributes {
            if !attr.path().is_ident("pspp") {
                continue;
            }
            attr.parse_nested_meta(|meta| {
                if meta.path.is_ident("syntax") {
                    meta.input.parse::<Token![=]>()?;
                    let syntax = meta.input.parse::<Literal>()?;
                    field_attrs.syntax = Some(syntax);
                } else if meta.path.is_ident("no_selector") {
                    field_attrs.selector = Some(false);
                } else {
                    return Err(Error::new(meta.path.span(), "Unknown attribute"));
                }
                Ok(())
            })?;
        }
        Ok(field_attrs)
    }
}

struct StructAttrs {
    syntax: Option<Literal>,
    selector: bool,
}

impl Default for StructAttrs {
    fn default() -> Self {
        Self {
            syntax: None,
            selector: true,
        }
    }
}

impl StructAttrs {
    fn parse(attributes: &[Attribute]) -> Result<Self, Error> {
        //println!("{:?}", &attributes);
        let mut field_attrs = Self::default();
        for attr in attributes {
            if !attr.path().is_ident("pspp") {
                continue;
            }
            attr.parse_nested_meta(|meta| {
                if meta.path.is_ident("syntax") {
                    meta.input.parse::<Token![=]>()?;
                    let syntax = meta.input.parse::<Literal>()?;
                    field_attrs.syntax = Some(syntax);
                } else if meta.path.is_ident("no_selector") {
                    field_attrs.selector = false;
                } else {
                    return Err(Error::new(meta.path.span(), "Unknown attribute"));
                }
                Ok(())
            })?;
        }
        Ok(field_attrs)
    }
}
