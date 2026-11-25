/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   get_next_line.h                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ilaakoub <ilaakoub@student.1337.ma>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/11/20 08:24:51 by ilaakoub          #+#    #+#             */
/*   Updated: 2026/11/20 08:24:54 by ilaakoub         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef GET_NEXT_LINE_H
# define GET_NEXT_LINE_H

# ifndef BUFFER_SIZE
#  define BUFFER_SIZE 5
# endif

# include <stdlib.h>
# include <unistd.h>

char	*get_next_line(int fd);
int		has_newline(char *str);
char	*ft_strjoin(char *s1, char *s2);
int	ft_strlen(char *str);
int	ft_strlcat(char *dst, char *src, int size);
char	*ft_free(char **ptr);

#endif
