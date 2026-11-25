/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   get_next_line.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ilaakoub <ilaakoub@student.1337.ma>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/11/20 08:24:38 by ilaakoub          #+#    #+#             */
/*   Updated: 2026/11/20 08:24:42 by ilaakoub         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "get_next_line.h"

static char	*join_free(char *save, char *buffer)
{
	char	*tmp;

	tmp = ft_strjoin(save, buffer);
	free(save);
	return (tmp);
}

static char	*read_to_save(int fd, char *save)
{
	char	*buffer;
	ssize_t	bytes_read;

	buffer = malloc(BUFFER_SIZE + 1);
	if (!buffer)
		return (ft_free(&save));
	bytes_read = 1;
	while (bytes_read > 0 && !has_newline(save))
	{
		bytes_read = read(fd, buffer, BUFFER_SIZE);
		if (bytes_read <= 0)
			break ;
		buffer[bytes_read] = '\0';
		save = join_free(save, buffer);
		if (!save)
			break ;
	}
	free(buffer);
	if (bytes_read == -1)
		return (ft_free(&save));
	return (save);
}

static char	*extract_line(char *save)
{
	char	*line;
	int	i;
	int	len;

	len = 0;
	while (save[len] && save[len] != '\n')
		len++;
	if (save[len] == '\n')
		len++;
	line = malloc(len + 1);
	if (!line)
		return (NULL);
	i = 0;
	while (i < len)
	{
		line[i] = save[i];
		i++;
	}
	line[i] = '\0';
	return (line);
}

static char	*update_save(char *save, int i)
{
	char	*new_save;
	int		save_len;
	int		j;

	save_len = ft_strlen(save);
	new_save = malloc(save_len - i + 1);
	if (!new_save)
		return (ft_free(&save));
	j = 0;
	while (j < save_len - i)
	{
		new_save[j] = save[j + i];
		j++;
	}
	new_save[j] = '\0';
	free(save);
	return (new_save);
}

char	*get_next_line(int fd)
{
	static char	*save;
	char		*line;

	if (BUFFER_SIZE <= 0 || fd < 0 || read(fd, NULL, 0) == -1)
		return (ft_free(&save));
	save = read_to_save(fd, save);
	if (!save || save[0] == '\0')
		return (ft_free(&save));
	line = extract_line(save);
	if (!line)
		return (ft_free(&save));
	save = update_save(save, ft_strlen(line));
	if (!save)
	{
		free(line);
		return (NULL);
	}
	return (line);
}
