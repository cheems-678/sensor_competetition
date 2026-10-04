// Adapted from shadcn/ui's MIT-licensed Button component. See THIRD_PARTY_NOTICES.md.
import * as React from 'react'
import { Slot } from '@radix-ui/react-slot'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '../lib/utils'

const buttonVariants = cva('button', {
  variants: {
    variant: { default: 'button-primary', outline: 'button-outline', ghost: 'button-ghost' },
    size: { default: '', icon: 'button-icon', small: 'button-small' },
  },
  defaultVariants: { variant: 'default', size: 'default' },
})

type Props = React.ComponentProps<'button'> & VariantProps<typeof buttonVariants> & { asChild?: boolean }
export function Button({ className, variant, size, asChild = false, ...props }: Props) {
  const Component = asChild ? Slot : 'button'
  return <Component className={cn(buttonVariants({ variant, size, className }))} {...props} />
}
